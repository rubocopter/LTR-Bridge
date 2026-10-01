#include <Windows.h>
#include <d3d9.h>
#include <d3d11.h>
#include <d3d11_4.h>
#include <dxgi1_2.h>
#include <wrl/client.h>

#include "../d3d9_real_bridge_probe/client.h"
#include "../d3d9_real_bridge_probe/protocol.h"
#include "managed_compat.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

namespace {

using Microsoft::WRL::ComPtr;

using Direct3DCreate9Fn = IDirect3D9 *(WINAPI *)(UINT);
using Direct3DCreate9ExFn = HRESULT(WINAPI *)(UINT, IDirect3D9Ex **);
using GetProcAddressFn = FARPROC(WINAPI *)(HMODULE, LPCSTR);
using D3DXGetShaderVersionFn = DWORD(WINAPI *)(const DWORD *);
using CreateDeviceFn = HRESULT(STDMETHODCALLTYPE *)(
    IDirect3D9 *, UINT, D3DDEVTYPE, HWND, DWORD, D3DPRESENT_PARAMETERS *,
    IDirect3DDevice9 **);
using DeviceReleaseFn = ULONG(STDMETHODCALLTYPE *)(IDirect3DDevice9 *);
using ResourceReleaseFn = ULONG(STDMETHODCALLTYPE *)(void *);
using BeginStateBlockFn = HRESULT(STDMETHODCALLTYPE *)(IDirect3DDevice9 *);
using CreateTextureFn = HRESULT(STDMETHODCALLTYPE *)(
    IDirect3DDevice9 *, UINT, UINT, UINT, DWORD, D3DFORMAT, D3DPOOL,
    IDirect3DTexture9 **, HANDLE *);
using CreateVolumeTextureFn = HRESULT(STDMETHODCALLTYPE *)(
    IDirect3DDevice9 *, UINT, UINT, UINT, UINT, DWORD, D3DFORMAT, D3DPOOL,
    IDirect3DVolumeTexture9 **, HANDLE *);
using CreateCubeTextureFn = HRESULT(STDMETHODCALLTYPE *)(
    IDirect3DDevice9 *, UINT, UINT, DWORD, D3DFORMAT, D3DPOOL,
    IDirect3DCubeTexture9 **, HANDLE *);
using CreateVertexBufferFn = HRESULT(STDMETHODCALLTYPE *)(
    IDirect3DDevice9 *, UINT, DWORD, DWORD, D3DPOOL,
    IDirect3DVertexBuffer9 **, HANDLE *);
using CreateIndexBufferFn = HRESULT(STDMETHODCALLTYPE *)(
    IDirect3DDevice9 *, UINT, DWORD, D3DFORMAT, D3DPOOL,
    IDirect3DIndexBuffer9 **, HANDLE *);
using CreateVertexDeclarationFn = HRESULT(STDMETHODCALLTYPE *)(
    IDirect3DDevice9 *, const D3DVERTEXELEMENT9 *,
    IDirect3DVertexDeclaration9 **);
using VertexBufferLockFn = HRESULT(STDMETHODCALLTYPE *)(
    IDirect3DVertexBuffer9 *, UINT, UINT, void **, DWORD);
using VertexBufferUnlockFn = HRESULT(STDMETHODCALLTYPE *)(IDirect3DVertexBuffer9 *);
using TextureLockRectFn = HRESULT(STDMETHODCALLTYPE *)(
    IDirect3DTexture9 *, UINT, D3DLOCKED_RECT *, const RECT *, DWORD);
using TextureUnlockRectFn = HRESULT(STDMETHODCALLTYPE *)(IDirect3DTexture9 *, UINT);
using TextureGetSurfaceLevelFn = HRESULT(STDMETHODCALLTYPE *)(
    IDirect3DTexture9 *, UINT, IDirect3DSurface9 **);
using CubeTextureLockRectFn = HRESULT(STDMETHODCALLTYPE *)(
    IDirect3DCubeTexture9 *, D3DCUBEMAP_FACES, UINT, D3DLOCKED_RECT *,
    const RECT *, DWORD);
using CubeTextureUnlockRectFn = HRESULT(STDMETHODCALLTYPE *)(
    IDirect3DCubeTexture9 *, D3DCUBEMAP_FACES, UINT);
using CubeTextureGetCubeMapSurfaceFn = HRESULT(STDMETHODCALLTYPE *)(
    IDirect3DCubeTexture9 *, D3DCUBEMAP_FACES, UINT, IDirect3DSurface9 **);
using SurfaceLockRectFn = HRESULT(STDMETHODCALLTYPE *)(
    IDirect3DSurface9 *, D3DLOCKED_RECT *, const RECT *, DWORD);
using SurfaceUnlockRectFn = HRESULT(STDMETHODCALLTYPE *)(IDirect3DSurface9 *);
using IndexBufferLockFn = HRESULT(STDMETHODCALLTYPE *)(
    IDirect3DIndexBuffer9 *, UINT, UINT, void **, DWORD);
using IndexBufferUnlockFn = HRESULT(STDMETHODCALLTYPE *)(IDirect3DIndexBuffer9 *);
using ResetFn = HRESULT(STDMETHODCALLTYPE *)(IDirect3DDevice9 *,
                                             D3DPRESENT_PARAMETERS *);
using PresentFn = HRESULT(STDMETHODCALLTYPE *)(IDirect3DDevice9 *, const RECT *,
                                               const RECT *, HWND,
                                               const RGNDATA *);
using EndSceneFn = HRESULT(STDMETHODCALLTYPE *)(IDirect3DDevice9 *);
using DrawPrimitiveFn = HRESULT(STDMETHODCALLTYPE *)(
    IDirect3DDevice9 *, D3DPRIMITIVETYPE, UINT, UINT);
using DrawIndexedPrimitiveFn = HRESULT(STDMETHODCALLTYPE *)(
    IDirect3DDevice9 *, D3DPRIMITIVETYPE, INT, UINT, UINT, UINT, UINT);
using SetTextureFn = HRESULT(STDMETHODCALLTYPE *)(IDirect3DDevice9 *, DWORD,
                                                  IDirect3DBaseTexture9 *);
using SetStreamSourceFn = HRESULT(STDMETHODCALLTYPE *)(
    IDirect3DDevice9 *, UINT, IDirect3DVertexBuffer9 *, UINT, UINT);
using SetIndicesFn = HRESULT(STDMETHODCALLTYPE *)(IDirect3DDevice9 *,
                                                  IDirect3DIndexBuffer9 *);

constexpr std::size_t kD3D9CreateDeviceIndex = 16;
constexpr std::size_t kDeviceReleaseIndex = 2;
constexpr std::size_t kResourceReleaseIndex = 2;
constexpr std::size_t kDeviceResetIndex = 16;
constexpr std::size_t kDevicePresentIndex = 17;
constexpr std::size_t kDeviceCreateTextureIndex = 23;
constexpr std::size_t kDeviceCreateVolumeTextureIndex = 24;
constexpr std::size_t kDeviceCreateCubeTextureIndex = 25;
constexpr std::size_t kDeviceCreateVertexBufferIndex = 26;
constexpr std::size_t kDeviceCreateIndexBufferIndex = 27;
constexpr std::size_t kDeviceEndSceneIndex = 42;
constexpr std::size_t kDeviceBeginStateBlockIndex = 60;
constexpr std::size_t kDeviceSetTextureIndex = 65;
constexpr std::size_t kDeviceDrawPrimitiveIndex = 81;
constexpr std::size_t kDeviceDrawIndexedPrimitiveIndex = 82;
constexpr std::size_t kDeviceCreateVertexDeclarationIndex = 86;
constexpr std::size_t kDeviceSetStreamSourceIndex = 100;
constexpr std::size_t kDeviceSetIndicesIndex = 104;
constexpr std::size_t kTextureLockRectIndex = 19;
constexpr std::size_t kTextureUnlockRectIndex = 20;
constexpr std::size_t kTextureGetSurfaceLevelIndex = 18;
constexpr std::size_t kCubeTextureLockRectIndex = 19;
constexpr std::size_t kCubeTextureUnlockRectIndex = 20;
constexpr std::size_t kCubeTextureGetCubeMapSurfaceIndex = 18;
constexpr std::size_t kSurfaceLockRectIndex = 13;
constexpr std::size_t kSurfaceUnlockRectIndex = 14;
constexpr std::size_t kVertexBufferLockIndex = 11;
constexpr std::size_t kVertexBufferUnlockIndex = 12;
constexpr std::size_t kIndexBufferLockIndex = 11;
constexpr std::size_t kIndexBufferUnlockIndex = 12;
constexpr std::uintptr_t kObservedD3D9CanonicalDeviceVtableRva = 0x1008;
constexpr std::uint64_t kSummaryFrame = 120;
constexpr std::uint32_t kManagedDrawStateSampleLimit = 128;

struct RealBridgeRuntime;

struct DeviceState {
  std::uint32_t id = 0;
  std::uint32_t resource_generation = 1;
  std::uint64_t end_scene_count = 0;
  std::uint64_t present_count = 0;
  std::uint32_t reset_count = 0;
  UINT max_swapchains = 0;
  bool first_sample_written = false;
  bool complete_sample_written = false;
  bool summary_written = false;
  bool render_target_observed = false;
  bool depth_observed = false;
  bool transforms_observed = false;
  bool transforms_changed = false;
  bool relay_test_attempted = false;
  bool video_apply_probe_attempted = false;
  std::uint32_t managed_draw_scan_generation = 0;
  std::uint32_t managed_draw_scan_samples = 0;
  std::uint64_t managed_draw_scan_stale_bindings = 0;
  std::uint64_t managed_draw_scan_query_failures = 0;
  D3DSURFACE_DESC render_target{};
  D3DSURFACE_DESC depth{};
  std::array<D3DMATRIX, 3> last_transforms{};
  bool have_last_transforms = false;
  std::shared_ptr<RealBridgeRuntime> multiframe;
};

Direct3DCreate9Fn g_real_create9 = nullptr;
GetProcAddressFn g_real_get_proc_address = nullptr;
CreateDeviceFn g_real_create_device = nullptr;
DeviceReleaseFn g_real_device_release = nullptr;
BeginStateBlockFn g_real_begin_state_block = nullptr;
CreateTextureFn g_real_create_texture = nullptr;
CreateVolumeTextureFn g_real_create_volume_texture = nullptr;
CreateCubeTextureFn g_real_create_cube_texture = nullptr;
CreateVertexBufferFn g_real_create_vertex_buffer = nullptr;
CreateIndexBufferFn g_real_create_index_buffer = nullptr;
CreateVertexDeclarationFn g_real_create_vertex_declaration = nullptr;
VertexBufferLockFn g_real_vertex_buffer_lock = nullptr;
VertexBufferUnlockFn g_real_vertex_buffer_unlock = nullptr;
TextureLockRectFn g_real_texture_lock_rect = nullptr;
TextureUnlockRectFn g_real_texture_unlock_rect = nullptr;
TextureGetSurfaceLevelFn g_real_texture_get_surface_level = nullptr;
CubeTextureLockRectFn g_real_cube_texture_lock_rect = nullptr;
CubeTextureUnlockRectFn g_real_cube_texture_unlock_rect = nullptr;
CubeTextureGetCubeMapSurfaceFn g_real_cube_texture_get_cube_map_surface = nullptr;
IndexBufferLockFn g_real_index_buffer_lock = nullptr;
IndexBufferUnlockFn g_real_index_buffer_unlock = nullptr;
ResetFn g_real_reset = nullptr;
PresentFn g_real_present = nullptr;
EndSceneFn g_real_end_scene = nullptr;
DrawPrimitiveFn g_real_draw_primitive = nullptr;
DrawIndexedPrimitiveFn g_real_draw_indexed_primitive = nullptr;
SetTextureFn g_real_set_texture = nullptr;
SetStreamSourceFn g_real_set_stream_source = nullptr;
SetIndicesFn g_real_set_indices = nullptr;

std::mutex g_state_mutex;
std::mutex g_log_mutex;
std::unordered_map<IDirect3DDevice9 *, DeviceState> g_devices;
std::uint32_t g_next_device_id = 1;
std::wstring g_log_path;
bool g_dependency_hooked = false;
bool g_install_logged = false;
PVOID g_exception_handler = nullptr;
HANDLE g_exception_log = INVALID_HANDLE_VALUE;
volatile LONG g_exception_count = 0;
volatile LONG g_create_texture_trace_count = 0;
volatile LONG g_create_volume_texture_trace_count = 0;
volatile LONG g_create_cube_texture_trace_count = 0;
volatile LONG g_create_vertex_buffer_trace_count = 0;
volatile LONG g_create_index_buffer_trace_count = 0;
volatile LONG g_create_vertex_declaration_trace_count = 0;
volatile LONG g_vertex_buffer_lock_trace_count = 0;
volatile LONG g_vertex_buffer_unlock_trace_count = 0;
volatile LONG g_managed_texture_lock_trace_count = 0;
volatile LONG g_managed_texture_unlock_trace_count = 0;
volatile LONG g_managed_cube_lock_trace_count = 0;
volatile LONG g_managed_cube_unlock_trace_count = 0;
volatile LONG g_managed_surface_lock_trace_count = 0;
volatile LONG g_managed_surface_unlock_trace_count = 0;
volatile LONG g_managed_index_buffer_lock_trace_count = 0;
volatile LONG g_managed_index_buffer_unlock_trace_count = 0;
volatile LONG g_managed_draw_binding_trace_count = 0;
volatile LONG g_present_vb_slot_trace_count = 0;
volatile LONG g_managed_vertex_buffer_fallback_ready = 0;
volatile LONG g_post_fallback_present_count = 0;
volatile LONG g_post_fallback_end_scene_count = 0;
volatile LONG g_device_vb_slot_write_watch_armed = 0;
std::uintptr_t g_device_vtable_address = 0;
std::uintptr_t g_device_vb_slot_address = 0;

enum class ManagedResourceKind : std::uint8_t {
  texture2d,
  cube_texture,
  vertex_buffer,
  index_buffer,
};

struct ManagedResourceTrace {
  std::uint32_t id = 0;
  ManagedResourceKind kind = ManagedResourceKind::texture2d;
  IDirect3DDevice9 *device = nullptr;
  std::uint32_t device_id = 0;
  std::uint32_t creation_generation = 0;
  std::uint32_t release_calls = 0;
  std::uint32_t binding_generation = 0;
  std::uint8_t binding_mask = 0;
};

enum ManagedBindingMask : std::uint8_t {
  kManagedBindingTexture = 1u << 0,
  kManagedBindingStreamSource = 1u << 1,
  kManagedBindingIndices = 1u << 2,
};

struct ManagedSurfaceTrace {
  std::uint32_t resource_id = 0;
  ManagedResourceKind parent_kind = ManagedResourceKind::texture2d;
  D3DCUBEMAP_FACES face = D3DCUBEMAP_FACE_POSITIVE_X;
  UINT level = 0;
};

struct SurfaceVtableOriginals {
  SurfaceLockRectFn lock_rect = nullptr;
  SurfaceUnlockRectFn unlock_rect = nullptr;
};

std::mutex g_managed_resource_mutex;
std::unordered_map<void *, ManagedResourceTrace> g_managed_resources;
std::unordered_map<void *, ManagedSurfaceTrace> g_managed_surfaces;
std::unordered_map<void **, SurfaceVtableOriginals> g_surface_vtable_originals;
std::unordered_map<void **, ResourceReleaseFn> g_managed_resource_release_originals;
std::uint32_t g_next_managed_resource_id = 1;

struct ManagedTextureContentFingerprint {
  UINT width = 0;
  UINT height = 0;
  D3DFORMAT format = D3DFMT_UNKNOWN;
  std::uint64_t hash = 0;
  std::uint64_t bytes = 0;
  bool valid = false;
};

std::unordered_map<std::uint32_t, ManagedTextureContentFingerprint>
    g_managed_texture_pre_reset_fingerprints;

struct ChromeFlowBreakpoint {
  std::size_t rva = 0;
  BYTE expected = 0;
  const char *label = nullptr;
  volatile LONG armed = 0;
  volatile LONG hit_count = 0;
};

std::array<ChromeFlowBreakpoint, 14> g_chrome_flow_breakpoints{{
    {0x23084F, 0x33, "primary_decl_result", 0},
    {0x2308F3, 0x3B, "secondary_decl_result", 0},
    {0x2309EB, 0x85, "direct_vb_result", 0},
    {0x22FEC3, 0xFF, "allocator_vb_call", 0},
    {0x22FEC6, 0x85, "allocator_vb_result", 0},
    {0x230AD5, 0x8B, "allocator_result", 0},
    {0x2345D9, 0x6A, "after_pre_begin_state_block", 0},
    {0x2345F1, 0x85, "after_pre_end_state_block", 0},
    {0x2348EC, 0x6A, "after_begin_state_block", 0},
    {0x234906, 0x85, "after_end_state_block", 0},
    {0x234979, 0x6A, "after_second_begin_state_block", 0},
    {0x234996, 0x85, "after_second_end_state_block", 0},
    {0x234B89, 0x6A, "after_third_begin_state_block", 0},
    {0x234BA1, 0x85, "after_third_end_state_block", 0},
}};
bool g_chrome_flow_probe_installed = false;
thread_local ChromeFlowBreakpoint *g_pending_chrome_breakpoint_rearm = nullptr;
thread_local bool g_pending_chrome_breakpoint_had_trap_flag = false;
thread_local bool g_managed_draw_query_release = false;

void append_line(const std::string &line);
[[nodiscard]] bool install_vertex_buffer_use_trace(IDirect3DVertexBuffer9 *buffer);
HRESULT STDMETHODCALLTYPE hook_surface_lock_rect(IDirect3DSurface9 *surface,
                                                  D3DLOCKED_RECT *locked_rect,
                                                  const RECT *rect,
                                                  DWORD flags);
HRESULT STDMETHODCALLTYPE hook_surface_unlock_rect(IDirect3DSurface9 *surface);
ULONG STDMETHODCALLTYPE hook_managed_resource_release(void *resource);
const char *managed_resource_kind_name(ManagedResourceKind kind);
HRESULT STDMETHODCALLTYPE hook_create_vertex_buffer(
    IDirect3DDevice9 *device, UINT length, DWORD usage, DWORD fvf, D3DPOOL pool,
    IDirect3DVertexBuffer9 **buffer, HANDLE *shared_handle);
HRESULT STDMETHODCALLTYPE hook_begin_state_block(IDirect3DDevice9 *device);
[[nodiscard]] bool
rehook_observer_device_hooks_after_state_block(IDirect3DDevice9 *device);

[[nodiscard]] bool canonical_vertex_buffer_hook_requested() {
  char value[8]{};
  const DWORD length = GetEnvironmentVariableA(
      "LTR_D3D9_CANONICAL_VB_HOOK", value,
      static_cast<DWORD>(std::size(value)));
  return length > 0 && length < std::size(value) && value[0] == '1';
}

[[nodiscard]] bool resource_census_requested() {
  char value[8]{};
  const DWORD length = GetEnvironmentVariableA(
      "LTR_D3D9_RESOURCE_CENSUS", value,
      static_cast<DWORD>(std::size(value)));
  return length > 0 && length < std::size(value) && value[0] == '1';
}

[[nodiscard]] bool managed_semantic_adaptation_requested() {
  char value[8]{};
  const DWORD length = GetEnvironmentVariableA(
      "LTR_D3D9_MANAGED_SEMANTIC_ADAPTATION", value,
      static_cast<DWORD>(std::size(value)));
  return length > 0 && length < std::size(value) && value[0] == '1';
}

[[nodiscard]] bool managed_reset_content_probe_requested() {
  char value[8]{};
  const DWORD length = GetEnvironmentVariableA(
      "LTR_D3D9_MANAGED_RESET_CONTENT_PROBE", value,
      static_cast<DWORD>(std::size(value)));
  return length > 0 && length < std::size(value) && value[0] == '1';
}

[[nodiscard]] bool video_apply_probe_requested() {
  char value[8]{};
  const DWORD length = GetEnvironmentVariableA(
      "LTR_D3D9_VIDEO_APPLY_PROBE", value,
      static_cast<DWORD>(std::size(value)));
  return length > 0 && length < std::size(value) && value[0] == '1';
}

[[nodiscard]] UINT video_apply_probe_dimension(const char *name,
                                               UINT fallback) {
  char value[16]{};
  const DWORD length = GetEnvironmentVariableA(
      name, value, static_cast<DWORD>(std::size(value)));
  if (length == 0 || length >= std::size(value))
    return fallback;
  char *end = nullptr;
  const unsigned long parsed = std::strtoul(value, &end, 10);
  if (!end || *end != '\0' || parsed == 0 ||
      parsed > std::numeric_limits<UINT>::max())
    return fallback;
  return static_cast<UINT>(parsed);
}

constexpr std::int32_t kJniOk = 0;
constexpr std::int32_t kJniDetached = -2;
constexpr std::int32_t kJniVersion14 = 0x00010004;
constexpr std::size_t kJniFindClass = 6;
constexpr std::size_t kJniExceptionOccurred = 15;
constexpr std::size_t kJniExceptionClear = 17;
constexpr std::size_t kJniDeleteLocalRef = 23;
constexpr std::size_t kJniGetStaticMethodId = 113;
constexpr std::size_t kJniCallStaticVoidMethodA = 143;
constexpr std::size_t kJniVmDetachCurrentThread = 5;
constexpr std::size_t kJniVmGetEnv = 6;
constexpr std::size_t kJniVmAttachCurrentThreadAsDaemon = 7;

union JniValue {
  std::uint8_t z;
  std::int8_t b;
  std::uint16_t c;
  std::int16_t s;
  std::int32_t i;
  std::int64_t j;
  float f;
  double d;
  void *l;
};
static_assert(sizeof(JniValue) == 8);

template <typename Function>
Function jni_env_function(void *env, std::size_t index) noexcept {
  if (!env)
    return nullptr;
  auto ***holder = reinterpret_cast<void ***>(env);
  if (!holder || !*holder)
    return nullptr;
  return reinterpret_cast<Function>((*holder)[index]);
}

template <typename Function>
Function jni_vm_function(void *vm, std::size_t index) noexcept {
  if (!vm)
    return nullptr;
  auto ***holder = reinterpret_cast<void ***>(vm);
  if (!holder || !*holder)
    return nullptr;
  return reinterpret_cast<Function>((*holder)[index]);
}

using JniGetCreatedJavaVmsFn =
    std::int32_t(__stdcall *)(void **, std::int32_t, std::int32_t *);
using JniGetEnvFn =
    std::int32_t(__stdcall *)(void *, void **, std::int32_t);
using JniAttachCurrentThreadFn =
    std::int32_t(__stdcall *)(void *, void **, void *);
using JniDetachCurrentThreadFn = std::int32_t(__stdcall *)(void *);
using JniFindClassFn = void *(__stdcall *)(void *, const char *);
using JniExceptionOccurredFn = void *(__stdcall *)(void *);
using JniExceptionClearFn = void(__stdcall *)(void *);
using JniDeleteLocalRefFn = void(__stdcall *)(void *, void *);
using JniGetStaticMethodIdFn =
    void *(__stdcall *)(void *, void *, const char *, const char *);
using JniCallStaticVoidMethodAFn =
    void(__stdcall *)(void *, void *, void *, const JniValue *);

[[nodiscard]] bool clear_video_apply_jni_exception(void *env,
                                                   const char *stage) {
  const auto occurred = jni_env_function<JniExceptionOccurredFn>(
      env, kJniExceptionOccurred);
  if (!occurred)
    return false;
  void *exception = occurred(env);
  if (!exception)
    return false;
  if (const auto clear =
          jni_env_function<JniExceptionClearFn>(env, kJniExceptionClear))
    clear(env);
  if (const auto delete_local =
          jni_env_function<JniDeleteLocalRefFn>(env, kJniDeleteLocalRef))
    delete_local(env, exception);
  std::ostringstream out;
  out << "event=video_apply_probe stage=" << stage
      << " result=jni_exception";
  append_line(out.str());
  return true;
}

void run_video_apply_probe(UINT current_width, UINT current_height) {
  const UINT width = video_apply_probe_dimension(
      "LTR_D3D9_VIDEO_APPLY_WIDTH", current_width);
  const UINT height = video_apply_probe_dimension(
      "LTR_D3D9_VIDEO_APPLY_HEIGHT", current_height);
  {
    std::ostringstream out;
    out << "event=video_apply_probe stage=begin current_width="
        << current_width << " current_height=" << current_height
        << " target_width=" << width << " target_height=" << height;
    append_line(out.str());
  }

  HMODULE jvm_module = GetModuleHandleA("jvm.dll");
  if (!jvm_module) {
    append_line("event=video_apply_probe stage=jvm result=module_missing");
    return;
  }
  const auto get_created = reinterpret_cast<JniGetCreatedJavaVmsFn>(
      GetProcAddress(jvm_module, "JNI_GetCreatedJavaVMs"));
  if (!get_created) {
    append_line("event=video_apply_probe stage=jvm result=entry_missing");
    return;
  }
  void *vm = nullptr;
  std::int32_t vm_count = 0;
  if (get_created(&vm, 1, &vm_count) != kJniOk || vm_count != 1 || !vm) {
    append_line("event=video_apply_probe stage=jvm result=vm_unavailable");
    return;
  }

  const auto get_env = jni_vm_function<JniGetEnvFn>(vm, kJniVmGetEnv);
  if (!get_env) {
    append_line("event=video_apply_probe stage=jni result=get_env_missing");
    return;
  }
  void *env = nullptr;
  bool attached_here = false;
  const std::int32_t get_env_result = get_env(vm, &env, kJniVersion14);
  if (get_env_result == kJniDetached) {
    const auto attach = jni_vm_function<JniAttachCurrentThreadFn>(
        vm, kJniVmAttachCurrentThreadAsDaemon);
    if (!attach || attach(vm, &env, nullptr) != kJniOk || !env) {
      append_line("event=video_apply_probe stage=jni result=attach_failed");
      return;
    }
    attached_here = true;
  } else if (get_env_result != kJniOk || !env) {
    append_line("event=video_apply_probe stage=jni result=get_env_failed");
    return;
  }

  const auto detach_if_needed = [&]() {
    if (!attached_here)
      return;
    if (const auto detach = jni_vm_function<JniDetachCurrentThreadFn>(
            vm, kJniVmDetachCurrentThread))
      detach(vm);
  };
  const auto find_class =
      jni_env_function<JniFindClassFn>(env, kJniFindClass);
  const auto get_static_method = jni_env_function<JniGetStaticMethodIdFn>(
      env, kJniGetStaticMethodId);
  const auto call_static_void = jni_env_function<JniCallStaticVoidMethodAFn>(
      env, kJniCallStaticVoidMethodA);
  const auto delete_local =
      jni_env_function<JniDeleteLocalRefFn>(env, kJniDeleteLocalRef);
  if (!find_class || !get_static_method || !call_static_void || !delete_local) {
    append_line("event=video_apply_probe stage=jni result=functions_missing");
    detach_if_needed();
    return;
  }

  void *game_object = find_class(env, "GameObject");
  if (!game_object || clear_video_apply_jni_exception(env, "find_class")) {
    if (game_object)
      delete_local(env, game_object);
    detach_if_needed();
    return;
  }
  void *set_resolution =
      get_static_method(env, game_object, "SetResolution", "(II)V");
  void *apply_video_settings =
      get_static_method(env, game_object, "ApplyVideoSettings", "()V");
  if (!set_resolution || !apply_video_settings ||
      clear_video_apply_jni_exception(env, "method_lookup")) {
    append_line("event=video_apply_probe stage=method_lookup result=failed");
    delete_local(env, game_object);
    detach_if_needed();
    return;
  }

  JniValue resolution_args[2]{};
  resolution_args[0].i = static_cast<std::int32_t>(width);
  resolution_args[1].i = static_cast<std::int32_t>(height);
  call_static_void(env, game_object, set_resolution, resolution_args);
  if (clear_video_apply_jni_exception(env, "set_resolution")) {
    delete_local(env, game_object);
    detach_if_needed();
    return;
  }
  append_line("event=video_apply_probe stage=set_resolution result=ok");

  call_static_void(env, game_object, apply_video_settings, nullptr);
  const bool apply_exception =
      clear_video_apply_jni_exception(env, "apply_video_settings");
  if (!apply_exception)
    append_line("event=video_apply_probe stage=apply_video_settings result=ok");
  delete_local(env, game_object);
  detach_if_needed();
}

[[nodiscard]] bool managed_resource_trace_requested() {
  return resource_census_requested() || managed_semantic_adaptation_requested();
}

[[nodiscard]] bool begin_state_block_vertex_buffer_rehook_requested() {
  if (resource_census_requested() || managed_semantic_adaptation_requested())
    return true;
  char value[8]{};
  const DWORD length = GetEnvironmentVariableA(
      "LTR_D3D9_BEGIN_STATE_BLOCK_VB_REHOOK", value,
      static_cast<DWORD>(std::size(value)));
  return length > 0 && length < std::size(value) && value[0] == '1';
}

[[nodiscard]] bool install_canonical_vertex_buffer_hook() {
  if (!g_real_create_vertex_buffer)
    return false;

  HMODULE d3d9 = GetModuleHandleA("d3d9.dll");
  if (!d3d9)
    return false;

  auto **canonical_vtable = reinterpret_cast<void **>(
      reinterpret_cast<std::uintptr_t>(d3d9) +
      kObservedD3D9CanonicalDeviceVtableRva);
  void **slot = &canonical_vtable[kDeviceCreateVertexBufferIndex];
  void *const previous = *slot;
  void *const expected = reinterpret_cast<void *>(g_real_create_vertex_buffer);
  void *const hook = reinterpret_cast<void *>(&hook_create_vertex_buffer);

  if (previous != expected && previous != hook) {
    std::ostringstream out;
    out << "event=canonical_vb_hook installed=0 reason=source_slot_mismatch"
        << " d3d9_base=0x" << std::hex
        << reinterpret_cast<std::uintptr_t>(d3d9)
        << " table=0x" << reinterpret_cast<std::uintptr_t>(canonical_vtable)
        << " previous=0x" << reinterpret_cast<std::uintptr_t>(previous)
        << " expected_original=0x" << reinterpret_cast<std::uintptr_t>(expected)
        << " hook=0x" << reinterpret_cast<std::uintptr_t>(hook) << std::dec;
    append_line(out.str());
    return false;
  }

  DWORD old_protect = 0;
  if (!VirtualProtect(slot, sizeof(*slot), PAGE_EXECUTE_READWRITE, &old_protect)) {
    append_line("event=canonical_vb_hook installed=0 reason=virtual_protect");
    return false;
  }
  *slot = hook;
  DWORD ignored = 0;
  VirtualProtect(slot, sizeof(*slot), old_protect, &ignored);
  FlushInstructionCache(GetCurrentProcess(), slot, sizeof(*slot));

  std::ostringstream out;
  out << "event=canonical_vb_hook installed=" << (*slot == hook)
      << " d3d9_base=0x" << std::hex
      << reinterpret_cast<std::uintptr_t>(d3d9)
      << " table=0x" << reinterpret_cast<std::uintptr_t>(canonical_vtable)
      << " slot=0x" << reinterpret_cast<std::uintptr_t>(slot)
      << " previous=0x" << reinterpret_cast<std::uintptr_t>(previous)
      << " original=0x" << reinterpret_cast<std::uintptr_t>(expected)
      << " hook=0x" << reinterpret_cast<std::uintptr_t>(hook) << std::dec;
  append_line(out.str());
  return *slot == hook;
}

[[nodiscard]] bool rehook_vertex_buffer_after_state_block(const char *point) {
  if (g_device_vtable_address == 0 || !g_real_create_vertex_buffer)
    return false;

  auto **vtable = reinterpret_cast<void **>(g_device_vtable_address);
  void **slot = &vtable[kDeviceCreateVertexBufferIndex];
  void *const previous = *slot;
  void *const hook = reinterpret_cast<void *>(&hook_create_vertex_buffer);

  DWORD old_protect = 0;
  if (!VirtualProtect(slot, sizeof(*slot), PAGE_EXECUTE_READWRITE, &old_protect))
    return false;
  *slot = hook;
  DWORD ignored = 0;
  VirtualProtect(slot, sizeof(*slot), old_protect, &ignored);
  FlushInstructionCache(GetCurrentProcess(), slot, sizeof(*slot));

  std::ostringstream out;
  out << "event=state_block_vb_rehook point=" << point << " previous=0x"
      << std::hex << reinterpret_cast<std::uintptr_t>(previous)
      << " expected_original=0x"
      << reinterpret_cast<std::uintptr_t>(g_real_create_vertex_buffer)
      << " hook=0x" << reinterpret_cast<std::uintptr_t>(hook)
      << std::dec << " installed=" << (*slot == hook);
  append_line(out.str());
  return *slot == hook;
}

[[nodiscard]] bool promote_to_d3d9ex_requested() {
  char value[8]{};
  const DWORD length = GetEnvironmentVariableA("LTR_D3D9_PROMOTE_EX", value,
                                                static_cast<DWORD>(std::size(value)));
  return length > 0 && length < std::size(value) && value[0] == '1';
}

[[nodiscard]] bool trace_exceptions_requested() {
  char value[8]{};
  const DWORD length = GetEnvironmentVariableA(
      "LTR_D3D9_TRACE_EXCEPTIONS", value, static_cast<DWORD>(std::size(value)));
  return length > 0 && length < std::size(value) && value[0] == '1';
}

[[nodiscard]] bool trace_chrome_flow_requested() {
  char value[8]{};
  const DWORD length = GetEnvironmentVariableA(
      "LTR_D3D9_TRACE_CHROME_FLOW", value,
      static_cast<DWORD>(std::size(value)));
  return length > 0 && length < std::size(value) && value[0] == '1';
}

[[nodiscard]] bool rearm_chrome_begin_breakpoints_requested() {
  char value[8]{};
  const DWORD length = GetEnvironmentVariableA(
      "LTR_D3D9_REARM_CHROME_BEGIN_BREAKPOINTS", value,
      static_cast<DWORD>(std::size(value)));
  return length > 0 && length < std::size(value) && value[0] == '1';
}

[[nodiscard]] bool managed_texture_fallback_requested() {
  char value[8]{};
  const DWORD length = GetEnvironmentVariableA(
      "LTR_D3D9_MANAGED_TEXTURE_FALLBACK", value,
      static_cast<DWORD>(std::size(value)));
  return length > 0 && length < std::size(value) && value[0] == '1';
}

[[nodiscard]] bool managed_vertex_buffer_fallback_requested() {
  char value[8]{};
  const DWORD length = GetEnvironmentVariableA(
      "LTR_D3D9_MANAGED_VERTEX_BUFFER_FALLBACK", value,
      static_cast<DWORD>(std::size(value)));
  return length > 0 && length < std::size(value) && value[0] == '1';
}

[[nodiscard]] bool managed_index_buffer_fallback_requested() {
  char value[8]{};
  const DWORD length = GetEnvironmentVariableA(
      "LTR_D3D9_MANAGED_INDEX_BUFFER_FALLBACK", value,
      static_cast<DWORD>(std::size(value)));
  return length > 0 && length < std::size(value) && value[0] == '1';
}

[[nodiscard]] bool shared_relay_test_requested() {
  char value[8]{};
  const DWORD length = GetEnvironmentVariableA("LTR_D3D9_TEST_RELAY", value,
                                                static_cast<DWORD>(std::size(value)));
  return length > 0 && length < std::size(value) && value[0] == '1';
}

[[nodiscard]] bool x86_x64_bridge_test_requested() {
  char value[8]{};
  const DWORD length = GetEnvironmentVariableA(
      "LTR_D3D9_TEST_X86_X64", value, static_cast<DWORD>(std::size(value)));
  return length > 0 && length < std::size(value) && value[0] == '1';
}

[[nodiscard]] bool multiframe_bridge_test_requested() {
  char value[8]{};
  const DWORD length = GetEnvironmentVariableA(
      "LTR_D3D9_TEST_MULTIFRAME", value, static_cast<DWORD>(std::size(value)));
  return length > 0 && length < std::size(value) && value[0] == '1';
}

[[nodiscard]] std::uint32_t multiframe_initial_stall_ms() {
  char value[16]{};
  const DWORD length = GetEnvironmentVariableA(
      "LTR_D3D9_MULTIFRAME_STALL_MS", value,
      static_cast<DWORD>(std::size(value)));
  if (length == 0 || length >= std::size(value))
    return 0;
  std::uint64_t parsed = 0;
  for (DWORD i = 0; i < length; ++i) {
    if (value[i] < '0' || value[i] > '9')
      return 0;
    parsed = parsed * 10ULL + static_cast<std::uint64_t>(value[i] - '0');
    if (parsed > 60'000ULL)
      return 60'000U;
  }
  return static_cast<std::uint32_t>(parsed);
}

[[nodiscard]] std::wstring x64_bridge_sink_path() {
  std::array<wchar_t, 32768> buffer{};
  const DWORD length = GetEnvironmentVariableW(
      L"LTR_D3D9_REAL_BRIDGE_SINK", buffer.data(),
      static_cast<DWORD>(buffer.size()));
  if (length == 0 || length >= buffer.size())
    return {};
  return std::wstring(buffer.data(), length);
}

[[nodiscard]] std::wstring quote_argument(const std::wstring &value) {
  return L"\"" + value + L"\"";
}

[[nodiscard]] HRESULT wait_d3d11_event(ID3D11Device *device,
                                       ID3D11DeviceContext *context) {
  D3D11_QUERY_DESC desc{};
  desc.Query = D3D11_QUERY_EVENT;
  ComPtr<ID3D11Query> query;
  HRESULT hr = device->CreateQuery(&desc, &query);
  if (FAILED(hr))
    return hr;
  context->End(query.Get());
  context->Flush();
  for (std::uint32_t attempt = 0; attempt < 10000; ++attempt) {
    hr = context->GetData(query.Get(), nullptr, 0, 0);
    if (hr == S_OK)
      return S_OK;
    if (FAILED(hr))
      return hr;
    Sleep(0);
  }
  return HRESULT_FROM_WIN32(WAIT_TIMEOUT);
}

[[nodiscard]] bool run_x64_bridge_sink(
    HANDLE shared_handle, UINT width, UINT height,
    const std::array<std::uint32_t, 5> &expected_samples) {
  const std::wstring sink = x64_bridge_sink_path();
  if (sink.empty()) {
    append_line("event=real_x86_x64_bridge stage=sink_path RESULT FAIL");
    return false;
  }

  wchar_t temp[MAX_PATH]{};
  const DWORD temp_length =
      GetTempPathW(static_cast<DWORD>(std::size(temp)), temp);
  if (temp_length == 0 || temp_length >= std::size(temp)) {
    append_line("event=real_x86_x64_bridge stage=temp_path RESULT FAIL");
    return false;
  }
  std::wostringstream log_builder;
  log_builder << temp << L"ltr_d3d9_real_bridge_sink_" << GetCurrentProcessId()
              << L".log";
  const std::wstring sink_log = log_builder.str();
  DeleteFileW(sink_log.c_str());

  std::wostringstream command;
  command << quote_argument(sink) << L" --handle "
          << static_cast<unsigned long long>(
                 reinterpret_cast<std::uintptr_t>(shared_handle))
          << L" --width " << width << L" --height " << height << L" --log "
          << quote_argument(sink_log);
  for (std::size_t index = 0; index < expected_samples.size(); ++index)
    command << L" --expected" << index << L" " << expected_samples[index];

  std::wstring command_line = command.str();
  STARTUPINFOW startup{};
  startup.cb = sizeof(startup);
  PROCESS_INFORMATION process{};
  if (!CreateProcessW(nullptr, command_line.data(), nullptr, nullptr, TRUE,
                      CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process)) {
    std::ostringstream out;
    out << "event=real_x86_x64_bridge stage=create_process error="
        << GetLastError() << " RESULT FAIL";
    append_line(out.str());
    return false;
  }
  CloseHandle(process.hThread);
  const DWORD wait = WaitForSingleObject(process.hProcess, 15000);
  DWORD exit_code = std::numeric_limits<DWORD>::max();
  if (wait == WAIT_OBJECT_0)
    GetExitCodeProcess(process.hProcess, &exit_code);
  CloseHandle(process.hProcess);

  HANDLE log_file = CreateFileW(sink_log.c_str(), GENERIC_READ,
                                FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                                OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (log_file != INVALID_HANDLE_VALUE) {
    LARGE_INTEGER size{};
    if (GetFileSizeEx(log_file, &size) && size.QuadPart > 0 &&
        size.QuadPart < 64 * 1024) {
      std::string text(static_cast<std::size_t>(size.QuadPart), '\0');
      DWORD read = 0;
      if (ReadFile(log_file, text.data(), static_cast<DWORD>(text.size()), &read,
                   nullptr) && read > 0) {
        text.resize(read);
        while (!text.empty() && (text.back() == '\r' || text.back() == '\n'))
          text.pop_back();
        if (!text.empty())
          append_line(text);
      }
    }
    CloseHandle(log_file);
  }
  DeleteFileW(sink_log.c_str());

  const bool passed = wait == WAIT_OBJECT_0 && exit_code == 0;
  std::ostringstream out;
  out << "event=real_x86_x64_bridge child_wait=" << wait
      << " child_exit=" << exit_code << " RESULT "
      << (passed ? "PASS" : "FAIL");
  append_line(out.str());
  return passed;
}

template <typename Fn>
[[nodiscard]] bool patch_iat(HMODULE module, const char *function_name, Fn hook,
                             Fn &original);
FARPROC WINAPI hook_get_proc_address(HMODULE module, LPCSTR name);

void ensure_engine_hook() {
  if (g_dependency_hooked && g_real_get_proc_address)
    return;
  HMODULE engine = GetModuleHandleA("ChromeEngine3.dll");
  if (engine) {
    g_dependency_hooked =
        patch_iat(engine, "GetProcAddress", hook_get_proc_address,
                  g_real_get_proc_address);
  }
}

[[nodiscard]] std::wstring log_path() {
  std::lock_guard lock(g_log_mutex);
  if (!g_log_path.empty())
    return g_log_path;

  wchar_t temp[MAX_PATH]{};
  const DWORD length = GetTempPathW(static_cast<DWORD>(std::size(temp)), temp);
  if (length == 0 || length >= std::size(temp))
    return L"ltr_d3d9_real_observer.log";

  std::wostringstream out;
  out << temp << L"ltr_d3d9_real_observer_" << GetCurrentProcessId() << L".log";
  g_log_path = out.str();
  return g_log_path;
}

void append_line(const std::string &line) {
  const std::wstring path = log_path();
  std::lock_guard lock(g_log_mutex);
  HANDLE file = CreateFileW(path.c_str(), FILE_APPEND_DATA,
                            FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                            OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (file == INVALID_HANDLE_VALUE)
    return;
  const std::string text = line + "\r\n";
  DWORD written = 0;
  WriteFile(file, text.data(), static_cast<DWORD>(text.size()), &written, nullptr);
  CloseHandle(file);
}

LONG CALLBACK trace_access_violation(EXCEPTION_POINTERS *pointers) {
  if (!pointers || !pointers->ExceptionRecord || !pointers->ContextRecord)
    return EXCEPTION_CONTINUE_SEARCH;

  EXCEPTION_RECORD &record = *pointers->ExceptionRecord;
  CONTEXT &context = *pointers->ContextRecord;

#if defined(_M_IX86)
  bool chrome_breakpoint_rearmed = false;
  if (record.ExceptionCode == EXCEPTION_SINGLE_STEP &&
      trace_chrome_flow_requested() && g_pending_chrome_breakpoint_rearm) {
    ChromeFlowBreakpoint *const breakpoint = g_pending_chrome_breakpoint_rearm;
    HMODULE engine = GetModuleHandleA("ChromeEngine3.dll");
    bool rearmed = false;
    if (engine) {
      auto *address = reinterpret_cast<BYTE *>(
          reinterpret_cast<std::uintptr_t>(engine) + breakpoint->rva);
      DWORD old_protect = 0;
      if (VirtualProtect(address, 1, PAGE_EXECUTE_READWRITE, &old_protect)) {
        if (*address == breakpoint->expected) {
          *address = 0xCC;
          FlushInstructionCache(GetCurrentProcess(), address, 1);
          rearmed = true;
        }
        DWORD ignored = 0;
        VirtualProtect(address, 1, old_protect, &ignored);
      }
    }
    if (rearmed)
      InterlockedExchange(&breakpoint->armed, 1);
    std::ostringstream out;
    out << "event=chrome_flow_breakpoint_rearm point=" << breakpoint->label
        << " hit=" << InterlockedCompareExchange(&breakpoint->hit_count, 0, 0)
        << " rearmed=" << rearmed;
    append_line(out.str());
    if (!g_pending_chrome_breakpoint_had_trap_flag)
      context.EFlags &= ~static_cast<DWORD>(0x100);
    g_pending_chrome_breakpoint_rearm = nullptr;
    g_pending_chrome_breakpoint_had_trap_flag = false;
    chrome_breakpoint_rearmed = true;
  }

  if (record.ExceptionCode == EXCEPTION_SINGLE_STEP &&
      trace_chrome_flow_requested() &&
      InterlockedCompareExchange(&g_device_vb_slot_write_watch_armed, 0, 1) ==
          1 &&
      (context.Dr6 & 0x1) != 0) {
    ULONG_PTR slot_value = 0;
    SIZE_T slot_bytes_read = 0;
    const BOOL slot_read_ok =
        g_device_vb_slot_address != 0 &&
        ReadProcessMemory(
            GetCurrentProcess(),
            reinterpret_cast<const void *>(g_device_vb_slot_address), &slot_value,
            sizeof(slot_value), &slot_bytes_read) &&
        slot_bytes_read == sizeof(slot_value);

    MEMORY_BASIC_INFORMATION memory{};
    char module[MAX_PATH]{};
    ULONG_PTR module_base = 0;
    const ULONG_PTR instruction = context.Eip;
    if (VirtualQuery(reinterpret_cast<const void *>(instruction), &memory,
                     sizeof(memory)) == sizeof(memory) &&
        memory.AllocationBase) {
      module_base = reinterpret_cast<ULONG_PTR>(memory.AllocationBase);
      GetModuleFileNameA(reinterpret_cast<HMODULE>(memory.AllocationBase), module,
                         static_cast<DWORD>(std::size(module)));
    }

    std::array<ULONG_PTR, 6> frame_returns{};
    ULONG_PTR frame_cursor = context.Ebp;
    for (auto &return_address : frame_returns) {
      if (frame_cursor == 0)
        break;
      ULONG_PTR frame_words[2]{};
      SIZE_T frame_bytes_read = 0;
      if (!ReadProcessMemory(GetCurrentProcess(),
                             reinterpret_cast<const void *>(frame_cursor),
                             frame_words, sizeof(frame_words),
                             &frame_bytes_read) ||
          frame_bytes_read != sizeof(frame_words))
        break;
      return_address = frame_words[1];
      if (frame_words[0] <= frame_cursor)
        break;
      frame_cursor = frame_words[0];
    }

    constexpr ULONG_PTR kClassicDeviceVtableBytes = 0x77 * sizeof(void *);
    const ULONG_PTR copied_vtable_source =
        context.Esi >= kClassicDeviceVtableBytes
            ? context.Esi - kClassicDeviceVtableBytes
            : 0;
    const ULONG_PTR copied_vtable_destination =
        context.Edi >= kClassicDeviceVtableBytes
            ? context.Edi - kClassicDeviceVtableBytes
            : 0;

    std::array<ULONG_PTR, 32> stack_words{};
    SIZE_T stack_bytes_read = 0;
    const BOOL stack_read_ok =
        ReadProcessMemory(GetCurrentProcess(),
                          reinterpret_cast<const void *>(context.Esp),
                          stack_words.data(), sizeof(stack_words),
                          &stack_bytes_read) &&
        stack_bytes_read == sizeof(stack_words);

    char line[4096]{};
    int length = std::snprintf(
        line, std::size(line),
        "event=device_vb_slot_write_watch thread=%lu instruction_after=0x%lx "
        "module_base=0x%llx module_offset=0x%llx module=%s slot_address=0x%llx "
        "slot_read=%d slot_value=0x%llx expected_hook=0x%llx original=0x%llx "
        "eax=0x%lx ebx=0x%lx ecx=0x%lx edx=0x%lx esi=0x%lx edi=0x%lx "
        "esp=0x%lx ebp=0x%lx copied_source=0x%llx copied_destination=0x%llx "
        "frame0=0x%llx frame1=0x%llx frame2=0x%llx frame3=0x%llx "
        "frame4=0x%llx frame5=0x%llx stack_read=%d stack_bytes=%llu "
        "dr6=0x%lx dr7=0x%lx",
        GetCurrentThreadId(), context.Eip,
        static_cast<unsigned long long>(module_base),
        static_cast<unsigned long long>(module_base ? instruction - module_base
                                                    : 0),
        module[0] ? module : "unknown",
        static_cast<unsigned long long>(g_device_vb_slot_address),
        slot_read_ok ? 1 : 0, static_cast<unsigned long long>(slot_value),
        static_cast<unsigned long long>(
            reinterpret_cast<std::uintptr_t>(&hook_create_vertex_buffer)),
        static_cast<unsigned long long>(
            reinterpret_cast<std::uintptr_t>(g_real_create_vertex_buffer)),
        context.Eax, context.Ebx, context.Ecx, context.Edx, context.Esi,
        context.Edi, context.Esp, context.Ebp,
        static_cast<unsigned long long>(copied_vtable_source),
        static_cast<unsigned long long>(copied_vtable_destination),
        static_cast<unsigned long long>(frame_returns[0]),
        static_cast<unsigned long long>(frame_returns[1]),
        static_cast<unsigned long long>(frame_returns[2]),
        static_cast<unsigned long long>(frame_returns[3]),
        static_cast<unsigned long long>(frame_returns[4]),
        static_cast<unsigned long long>(frame_returns[5]),
        stack_read_ok ? 1 : 0,
        static_cast<unsigned long long>(stack_bytes_read), context.Dr6,
        context.Dr7);
    if (length > 0 && length < static_cast<int>(sizeof(line)) && stack_read_ok) {
      for (std::size_t i = 0; i < stack_words.size(); ++i) {
        const int appended = std::snprintf(
            line + length, std::size(line) - static_cast<std::size_t>(length),
            " stack%02zu=0x%llx", i,
            static_cast<unsigned long long>(stack_words[i]));
        if (appended <= 0 ||
            appended >= static_cast<int>(std::size(line) -
                                         static_cast<std::size_t>(length)))
          break;
        length += appended;
      }
    }
    if (length > 0 && length < static_cast<int>(sizeof(line) - 2)) {
      line[length++] = '\r';
      line[length++] = '\n';
      line[length] = '\0';
    }
    if (length > 0 && g_exception_log != INVALID_HANDLE_VALUE) {
      DWORD written = 0;
      WriteFile(g_exception_log, line,
                static_cast<DWORD>((std::min)(
                    length, static_cast<int>(sizeof(line) - 1))),
                &written, nullptr);
      FlushFileBuffers(g_exception_log);
    }

    for (std::size_t level = 0; level < 2; ++level) {
      const ULONG_PTR return_address = frame_returns[level];
      if (return_address < 32 || g_exception_log == INVALID_HANDLE_VALUE)
        continue;

      MEMORY_BASIC_INFORMATION return_memory{};
      const SIZE_T queried = VirtualQuery(
          reinterpret_cast<const void *>(return_address), &return_memory,
          sizeof(return_memory));
      char return_module[MAX_PATH]{};
      if (queried == sizeof(return_memory) && return_memory.AllocationBase) {
        GetModuleFileNameA(reinterpret_cast<HMODULE>(return_memory.AllocationBase),
                           return_module,
                           static_cast<DWORD>(std::size(return_module)));
      }

      std::array<BYTE, 64> return_code{};
      SIZE_T return_code_bytes = 0;
      const ULONG_PTR return_code_start = return_address - 24;
      const BOOL return_code_read = ReadProcessMemory(
          GetCurrentProcess(),
          reinterpret_cast<const void *>(return_code_start), return_code.data(),
          return_code.size(), &return_code_bytes);

      char code_line[2048]{};
      int code_length = std::snprintf(
          code_line, std::size(code_line),
          "event=device_vtable_refresh_return_code level=%zu address=0x%llx "
          "query=%d allocation_base=0x%llx region_base=0x%llx region_size=0x%llx "
          "protect=0x%lx type=0x%lx module=%s code_start=0x%llx code_read=%d "
          "code_bytes=%llu code_hex=",
          level, static_cast<unsigned long long>(return_address),
          queried == sizeof(return_memory) ? 1 : 0,
          static_cast<unsigned long long>(reinterpret_cast<ULONG_PTR>(
              return_memory.AllocationBase)),
          static_cast<unsigned long long>(
              reinterpret_cast<ULONG_PTR>(return_memory.BaseAddress)),
          static_cast<unsigned long long>(return_memory.RegionSize),
          return_memory.Protect, return_memory.Type,
          return_module[0] ? return_module : "private_or_unknown",
          static_cast<unsigned long long>(return_code_start),
          return_code_read ? 1 : 0,
          static_cast<unsigned long long>(return_code_bytes));
      if (code_length > 0 && code_length < static_cast<int>(sizeof(code_line)) &&
          return_code_read) {
        const SIZE_T bytes_to_log = (std::min)(
            return_code_bytes, static_cast<SIZE_T>(return_code.size()));
        for (SIZE_T i = 0; i < bytes_to_log; ++i) {
          const int appended = std::snprintf(
              code_line + code_length,
              std::size(code_line) - static_cast<std::size_t>(code_length),
              "%02x", return_code[i]);
          if (appended <= 0 ||
              appended >= static_cast<int>(std::size(code_line) -
                                           static_cast<std::size_t>(code_length)))
            break;
          code_length += appended;
        }
      }
      if (code_length > 0 &&
          code_length < static_cast<int>(sizeof(code_line) - 2)) {
        code_line[code_length++] = '\r';
        code_line[code_length++] = '\n';
        code_line[code_length] = '\0';
      }
      if (code_length > 0) {
        DWORD written = 0;
        WriteFile(g_exception_log, code_line,
                  static_cast<DWORD>((std::min)(
                      code_length, static_cast<int>(sizeof(code_line) - 1))),
                  &written, nullptr);
        FlushFileBuffers(g_exception_log);
      }
    }

    context.Dr0 = 0;
    context.Dr6 = 0;
    context.Dr7 &= ~static_cast<DWORD>(0x000F0001);
    InterlockedExchange(&g_device_vb_slot_write_watch_armed, 0);
    return EXCEPTION_CONTINUE_EXECUTION;
  }
  if (chrome_breakpoint_rearmed)
    return EXCEPTION_CONTINUE_EXECUTION;
#endif

  if (record.ExceptionCode == EXCEPTION_BREAKPOINT &&
      trace_chrome_flow_requested()) {
    const auto instruction = reinterpret_cast<std::uintptr_t>(record.ExceptionAddress);
    HMODULE engine = GetModuleHandleA("ChromeEngine3.dll");
    const auto engine_base = reinterpret_cast<std::uintptr_t>(engine);
    if (engine && instruction >= engine_base) {
      const std::size_t rva = static_cast<std::size_t>(instruction - engine_base);
      for (auto &breakpoint : g_chrome_flow_breakpoints) {
        if (breakpoint.rva != rva ||
            InterlockedCompareExchange(&breakpoint.armed, 0, 1) != 1)
          continue;

        const LONG breakpoint_hit = InterlockedIncrement(&breakpoint.hit_count);
        const bool reusable_breakpoint =
            rearm_chrome_begin_breakpoints_requested() &&
            std::strstr(breakpoint.label, "begin_state_block") != nullptr;

        auto *address = reinterpret_cast<BYTE *>(engine_base + breakpoint.rva);
        DWORD old_protect = 0;
        if (VirtualProtect(address, 1, PAGE_EXECUTE_READWRITE, &old_protect)) {
          *address = breakpoint.expected;
          FlushInstructionCache(GetCurrentProcess(), address, 1);
          DWORD ignored = 0;
          VirtualProtect(address, 1, old_protect, &ignored);
        }

#if defined(_M_IX86)
        ULONG_PTR stack_words[8]{};
        SIZE_T stack_bytes_read = 0;
        const BOOL stack_read_ok = ReadProcessMemory(
            GetCurrentProcess(), reinterpret_cast<const void *>(context.Esp),
            stack_words, sizeof(stack_words), &stack_bytes_read);
        ULONG_PTR device_vtable = 0;
        ULONG_PTR device_vertex_buffer_slot = 0;
        SIZE_T device_pointer_bytes_read = 0;
        SIZE_T device_slot_bytes_read = 0;
        const bool allocator_vb_call =
            std::strcmp(breakpoint.label, "allocator_vb_call") == 0;
        const BOOL device_vtable_read_ok =
            allocator_vb_call && context.Ecx != 0 &&
            ReadProcessMemory(
                GetCurrentProcess(),
                reinterpret_cast<const void *>(context.Ecx), &device_vtable,
                sizeof(device_vtable), &device_pointer_bytes_read) &&
            device_pointer_bytes_read == sizeof(device_vtable) &&
            device_vtable != 0;
        const BOOL device_slot_read_ok =
            device_vtable_read_ok &&
            ReadProcessMemory(
                GetCurrentProcess(),
                reinterpret_cast<const void *>(
                    device_vtable + kDeviceCreateVertexBufferIndex * sizeof(void *)),
                &device_vertex_buffer_slot, sizeof(device_vertex_buffer_slot),
                &device_slot_bytes_read) &&
            device_slot_bytes_read == sizeof(device_vertex_buffer_slot);
        bool write_watch_armed_now = false;
        if (std::strcmp(breakpoint.label, "primary_decl_result") == 0 &&
            InterlockedCompareExchange(&g_device_vb_slot_write_watch_armed, 0,
                                       0) == 0 &&
            g_device_vb_slot_address != 0) {
          ULONG_PTR current_slot = 0;
          SIZE_T current_slot_bytes_read = 0;
          const BOOL current_slot_read_ok =
              ReadProcessMemory(
                  GetCurrentProcess(),
                  reinterpret_cast<const void *>(g_device_vb_slot_address),
                  &current_slot, sizeof(current_slot),
                  &current_slot_bytes_read) &&
              current_slot_bytes_read == sizeof(current_slot);
          if (current_slot_read_ok &&
              current_slot == reinterpret_cast<ULONG_PTR>(
                                  &hook_create_vertex_buffer)) {
            context.Dr0 = static_cast<DWORD>(g_device_vb_slot_address);
            context.Dr6 = 0;
            context.Dr7 &= ~static_cast<DWORD>(0x000F0001);
            context.Dr7 |= static_cast<DWORD>(0x000D0001);
            InterlockedExchange(&g_device_vb_slot_write_watch_armed, 1);
            write_watch_armed_now = true;
          }
        }
        char line[1024]{};
        const int length = std::snprintf(
            line, std::size(line),
             "event=chrome_flow_trace point=%s module_offset=0x%zx "
             "hit=%ld "
             "eax=0x%lx ebx=0x%lx ecx=0x%lx edx=0x%lx esi=0x%lx edi=0x%lx "
            "esp=0x%lx stack_read=%d stack_bytes=%llu stack4=0x%llx "
            "stack7=0x%llx device_vtable_read=%d device_vtable=0x%llx "
            "device_vb_slot_read=%d device_vb_slot=0x%llx "
            "vb_slot_write_watch_armed=%d watch_address=0x%llx\r\n",
             breakpoint.label, breakpoint.rva, breakpoint_hit, context.Eax, context.Ebx,
            context.Ecx, context.Edx, context.Esi, context.Edi, context.Esp,
            stack_read_ok ? 1 : 0,
            static_cast<unsigned long long>(stack_bytes_read),
            static_cast<unsigned long long>(stack_words[4]),
            static_cast<unsigned long long>(stack_words[7]),
            device_vtable_read_ok ? 1 : 0,
            static_cast<unsigned long long>(device_vtable),
            device_slot_read_ok ? 1 : 0,
            static_cast<unsigned long long>(device_vertex_buffer_slot),
            write_watch_armed_now ? 1 : 0,
            static_cast<unsigned long long>(g_device_vb_slot_address));
        if (length > 0 && g_exception_log != INVALID_HANDLE_VALUE) {
          DWORD written = 0;
          WriteFile(g_exception_log, line,
                    static_cast<DWORD>((std::min)(
                        length, static_cast<int>(sizeof(line) - 1))),
                    &written, nullptr);
          FlushFileBuffers(g_exception_log);
        }
        const bool state_block_breakpoint = reusable_breakpoint;
        if (state_block_breakpoint &&
            g_device_vtable_address != 0) {
          const auto read_device_slot = [](std::size_t index,
                                           ULONG_PTR &value) -> bool {
            SIZE_T bytes_read = 0;
            return ReadProcessMemory(
                       GetCurrentProcess(),
                       reinterpret_cast<const void *>(
                           g_device_vtable_address + index * sizeof(void *)),
                       &value, sizeof(value), &bytes_read) &&
                   bytes_read == sizeof(value);
          };
          ULONG_PTR reset_slot = 0;
          ULONG_PTR present_slot = 0;
          ULONG_PTR texture_slot = 0;
          ULONG_PTR cube_slot = 0;
          ULONG_PTR vertex_buffer_slot = 0;
          ULONG_PTR vertex_declaration_slot = 0;
          const bool reset_read = read_device_slot(kDeviceResetIndex, reset_slot);
          const bool present_read =
              read_device_slot(kDevicePresentIndex, present_slot);
          const bool texture_read =
              read_device_slot(kDeviceCreateTextureIndex, texture_slot);
          const bool cube_read =
              read_device_slot(kDeviceCreateCubeTextureIndex, cube_slot);
          const bool vertex_buffer_read = read_device_slot(
              kDeviceCreateVertexBufferIndex, vertex_buffer_slot);
          const bool vertex_declaration_read = read_device_slot(
              kDeviceCreateVertexDeclarationIndex, vertex_declaration_slot);
          char state_block_line[2048]{};
          const int state_block_length = std::snprintf(
              state_block_line, std::size(state_block_line),
              "event=state_block_vtable_snapshot point=%s "
              "reset_read=%d reset=0x%llx reset_original=0x%llx reset_is_original=%d "
              "present_read=%d present=0x%llx present_original=0x%llx present_is_original=%d "
              "texture_read=%d texture=0x%llx texture_original=0x%llx texture_is_original=%d "
              "cube_read=%d cube=0x%llx cube_original=0x%llx cube_is_original=%d "
              "vb_read=%d vb=0x%llx vb_original=0x%llx vb_is_original=%d "
              "decl_read=%d decl=0x%llx decl_original=0x%llx decl_is_original=%d\r\n",
              breakpoint.label, reset_read ? 1 : 0,
              static_cast<unsigned long long>(reset_slot),
              static_cast<unsigned long long>(
                  reinterpret_cast<ULONG_PTR>(g_real_reset)),
              reset_read && reset_slot == reinterpret_cast<ULONG_PTR>(g_real_reset),
              present_read ? 1 : 0,
              static_cast<unsigned long long>(present_slot),
              static_cast<unsigned long long>(
                  reinterpret_cast<ULONG_PTR>(g_real_present)),
              present_read &&
                  present_slot == reinterpret_cast<ULONG_PTR>(g_real_present),
              texture_read ? 1 : 0,
              static_cast<unsigned long long>(texture_slot),
              static_cast<unsigned long long>(
                  reinterpret_cast<ULONG_PTR>(g_real_create_texture)),
              texture_read && texture_slot ==
                                  reinterpret_cast<ULONG_PTR>(g_real_create_texture),
              cube_read ? 1 : 0,
              static_cast<unsigned long long>(cube_slot),
              static_cast<unsigned long long>(
                  reinterpret_cast<ULONG_PTR>(g_real_create_cube_texture)),
              cube_read && cube_slot == reinterpret_cast<ULONG_PTR>(
                                            g_real_create_cube_texture),
              vertex_buffer_read ? 1 : 0,
              static_cast<unsigned long long>(vertex_buffer_slot),
              static_cast<unsigned long long>(
                  reinterpret_cast<ULONG_PTR>(g_real_create_vertex_buffer)),
              vertex_buffer_read && vertex_buffer_slot ==
                                        reinterpret_cast<ULONG_PTR>(
                                            g_real_create_vertex_buffer),
              vertex_declaration_read ? 1 : 0,
              static_cast<unsigned long long>(vertex_declaration_slot),
              static_cast<unsigned long long>(reinterpret_cast<ULONG_PTR>(
                  g_real_create_vertex_declaration)),
              vertex_declaration_read && vertex_declaration_slot ==
                                             reinterpret_cast<ULONG_PTR>(
                                                 g_real_create_vertex_declaration));
          if (state_block_length > 0 &&
              g_exception_log != INVALID_HANDLE_VALUE) {
            DWORD written = 0;
            WriteFile(g_exception_log, state_block_line,
                      static_cast<DWORD>((std::min)(
                          state_block_length,
                          static_cast<int>(sizeof(state_block_line) - 1))),
                      &written, nullptr);
            FlushFileBuffers(g_exception_log);
          }
        }
        const bool after_state_block_begin =
            std::strstr(breakpoint.label, "begin_state_block") != nullptr;
        if (managed_vertex_buffer_fallback_requested() &&
            !begin_state_block_vertex_buffer_rehook_requested() &&
            after_state_block_begin) {
          const bool vertex_buffer_rehooked =
              rehook_vertex_buffer_after_state_block(breakpoint.label);
          if (vertex_buffer_rehooked && g_device_vb_slot_address != 0) {
            context.Dr0 = static_cast<DWORD>(g_device_vb_slot_address);
            context.Dr6 = 0;
            context.Dr7 &= ~static_cast<DWORD>(0x000F0001);
            context.Dr7 |= static_cast<DWORD>(0x000D0001);
            InterlockedExchange(&g_device_vb_slot_write_watch_armed, 1);
            append_line("event=state_block_vb_rehook_watch armed=1");
          }
        }
        if (managed_vertex_buffer_fallback_requested() &&
            std::strcmp(breakpoint.label, "allocator_vb_result") == 0 &&
            SUCCEEDED(static_cast<HRESULT>(context.Eax))) {
          InterlockedExchange(&g_post_fallback_present_count, 0);
          InterlockedExchange(&g_post_fallback_end_scene_count, 0);
          InterlockedExchange(&g_managed_vertex_buffer_fallback_ready, 1);
          IDirect3DVertexBuffer9 *buffer = nullptr;
          SIZE_T pointer_bytes_read = 0;
          if (ReadProcessMemory(GetCurrentProcess(),
                                reinterpret_cast<const void *>(context.Esi),
                                &buffer, sizeof(buffer), &pointer_bytes_read) &&
              pointer_bytes_read == sizeof(buffer) && buffer) {
            (void)install_vertex_buffer_use_trace(buffer);
          }
        }
        if (reusable_breakpoint) {
          g_pending_chrome_breakpoint_rearm = &breakpoint;
          g_pending_chrome_breakpoint_had_trap_flag =
              (context.EFlags & static_cast<DWORD>(0x100)) != 0;
          context.EFlags |= static_cast<DWORD>(0x100);
        }
        context.Eip = static_cast<DWORD>(instruction);
#endif
        return EXCEPTION_CONTINUE_EXECUTION;
      }
    }
    return EXCEPTION_CONTINUE_SEARCH;
  }

  if (record.ExceptionCode != EXCEPTION_ACCESS_VIOLATION)
    return EXCEPTION_CONTINUE_SEARCH;

  const LONG ordinal = InterlockedIncrement(&g_exception_count);
  if (ordinal > 8 || g_exception_log == INVALID_HANDLE_VALUE)
    return EXCEPTION_CONTINUE_SEARCH;

  const ULONG_PTR operation = record.NumberParameters >= 1
                                  ? record.ExceptionInformation[0]
                                  : static_cast<ULONG_PTR>(~0u);
  const ULONG_PTR fault_address = record.NumberParameters >= 2
                                      ? record.ExceptionInformation[1]
                                      : 0;

#if defined(_M_IX86)
  const ULONG_PTR instruction = context.Eip;
  const ULONG_PTR stack = context.Esp;
  const ULONG_PTR frame = context.Ebp;
  const ULONG_PTR eax = context.Eax;
  const ULONG_PTR ebx = context.Ebx;
  const ULONG_PTR ecx = context.Ecx;
  const ULONG_PTR edx = context.Edx;
  const ULONG_PTR esi = context.Esi;
  const ULONG_PTR edi = context.Edi;
  ULONG_PTR stack_words[8]{};
  SIZE_T stack_bytes_read = 0;
  const BOOL stack_read_ok = ReadProcessMemory(
      GetCurrentProcess(), reinterpret_cast<const void *>(stack), stack_words,
      sizeof(stack_words), &stack_bytes_read);
#else
  const ULONG_PTR instruction =
      reinterpret_cast<ULONG_PTR>(record.ExceptionAddress);
  const ULONG_PTR stack = 0;
  const ULONG_PTR frame = 0;
  const ULONG_PTR eax = 0;
  const ULONG_PTR ebx = 0;
  const ULONG_PTR ecx = 0;
  const ULONG_PTR edx = 0;
  const ULONG_PTR esi = 0;
  const ULONG_PTR edi = 0;
  ULONG_PTR stack_words[8]{};
  SIZE_T stack_bytes_read = 0;
  const BOOL stack_read_ok = FALSE;
#endif

  MEMORY_BASIC_INFORMATION memory{};
  char module[MAX_PATH]{};
  ULONG_PTR module_base = 0;
  if (VirtualQuery(reinterpret_cast<const void *>(instruction), &memory,
                   sizeof(memory)) == sizeof(memory) &&
      memory.AllocationBase) {
    module_base = reinterpret_cast<ULONG_PTR>(memory.AllocationBase);
    GetModuleFileNameA(reinterpret_cast<HMODULE>(memory.AllocationBase), module,
                       static_cast<DWORD>(std::size(module)));
  }

  char line[1536]{};
  const int length = std::snprintf(
      line, std::size(line),
      "event=exception_trace ordinal=%ld code=0x%08lx operation=%llu "
      "fault=0x%llx instruction=0x%llx module_base=0x%llx "
      "module_offset=0x%llx module=%s eax=0x%llx ebx=0x%llx ecx=0x%llx "
      "edx=0x%llx esi=0x%llx edi=0x%llx esp=0x%llx ebp=0x%llx "
      "stack_read=%d stack_bytes=%llu stack0=0x%llx stack1=0x%llx "
      "stack2=0x%llx stack3=0x%llx stack4=0x%llx stack5=0x%llx "
      "stack6=0x%llx stack7=0x%llx\r\n",
      ordinal, static_cast<unsigned long>(record.ExceptionCode),
      static_cast<unsigned long long>(operation),
      static_cast<unsigned long long>(fault_address),
      static_cast<unsigned long long>(instruction),
      static_cast<unsigned long long>(module_base),
      static_cast<unsigned long long>(module_base ? instruction - module_base
                                                 : 0),
      module[0] ? module : "unknown", static_cast<unsigned long long>(eax),
      static_cast<unsigned long long>(ebx), static_cast<unsigned long long>(ecx),
      static_cast<unsigned long long>(edx), static_cast<unsigned long long>(esi),
      static_cast<unsigned long long>(edi), static_cast<unsigned long long>(stack),
      static_cast<unsigned long long>(frame), stack_read_ok ? 1 : 0,
      static_cast<unsigned long long>(stack_bytes_read),
      static_cast<unsigned long long>(stack_words[0]),
      static_cast<unsigned long long>(stack_words[1]),
      static_cast<unsigned long long>(stack_words[2]),
      static_cast<unsigned long long>(stack_words[3]),
      static_cast<unsigned long long>(stack_words[4]),
      static_cast<unsigned long long>(stack_words[5]),
      static_cast<unsigned long long>(stack_words[6]),
      static_cast<unsigned long long>(stack_words[7]));
  if (length > 0) {
    DWORD written = 0;
    WriteFile(g_exception_log, line,
              static_cast<DWORD>((std::min)(length,
                                            static_cast<int>(sizeof(line) - 1))),
              &written, nullptr);
    FlushFileBuffers(g_exception_log);
  }
  return EXCEPTION_CONTINUE_SEARCH;
}

void install_chrome_flow_probe() {
  if (!trace_chrome_flow_requested() || g_chrome_flow_probe_installed ||
      !g_exception_handler)
    return;

  HMODULE engine = GetModuleHandleA("ChromeEngine3.dll");
  if (!engine) {
    append_line("event=chrome_flow_probe installed=0 reason=engine_missing");
    return;
  }

  const auto *base = reinterpret_cast<const BYTE *>(engine);
  const auto *dos = reinterpret_cast<const IMAGE_DOS_HEADER *>(base);
  if (dos->e_magic != IMAGE_DOS_SIGNATURE) {
    append_line("event=chrome_flow_probe installed=0 reason=bad_dos_header");
    return;
  }
  const auto *nt = reinterpret_cast<const IMAGE_NT_HEADERS32 *>(base + dos->e_lfanew);
  if (nt->Signature != IMAGE_NT_SIGNATURE ||
      nt->FileHeader.TimeDateStamp != 0x46AF2658 ||
      nt->OptionalHeader.SizeOfImage != 0x0060C000) {
    append_line("event=chrome_flow_probe installed=0 reason=build_metadata_mismatch");
    return;
  }

  for (const auto &breakpoint : g_chrome_flow_breakpoints) {
    if (base[breakpoint.rva] != breakpoint.expected) {
      std::ostringstream out;
      out << "event=chrome_flow_probe installed=0 reason=opcode_mismatch offset=0x"
          << std::hex << breakpoint.rva << " expected=0x"
          << static_cast<unsigned>(breakpoint.expected) << " actual=0x"
          << static_cast<unsigned>(base[breakpoint.rva]);
      append_line(out.str());
      return;
    }
  }

  if (managed_vertex_buffer_fallback_requested()) {
    append_line(std::string("event=managed_vertex_buffer_fallback armed=1 mechanism=") +
                (begin_state_block_vertex_buffer_rehook_requested()
                     ? "device_begin_state_block_hook "
                     : "post_begin_state_block_rehook ") +
                "length=0x40000 usage=0x8 fvf=0x0 pool_from=1 pool_to=0 "
                "shared_handle=0");
  }
  if (managed_index_buffer_fallback_requested()) {
    append_line(
        "event=managed_index_buffer_fallback armed=1 length=0x20000 usage=0x8 "
        "format=101 pool_from=1 pool_to=0 shared_handle=0");
  }

  for (auto &breakpoint : g_chrome_flow_breakpoints) {
    auto *address = const_cast<BYTE *>(base + breakpoint.rva);
    DWORD old_protect = 0;
    if (!VirtualProtect(address, 1, PAGE_EXECUTE_READWRITE, &old_protect)) {
      append_line("event=chrome_flow_probe installed=0 reason=virtual_protect");
      return;
    }
    *address = 0xCC;
    FlushInstructionCache(GetCurrentProcess(), address, 1);
    DWORD ignored = 0;
    VirtualProtect(address, 1, old_protect, &ignored);
    InterlockedExchange(&breakpoint.armed, 1);
  }
  g_chrome_flow_probe_installed = true;
  append_line("event=chrome_flow_probe installed=1 build=coj_1.1.1.0st points=8");
}

void install_exception_trace() {
  if (!trace_exceptions_requested() || g_exception_handler)
    return;
  const std::wstring path = log_path();
  g_exception_log = CreateFileW(path.c_str(), FILE_APPEND_DATA,
                                FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                                OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (g_exception_log == INVALID_HANDLE_VALUE) {
    append_line("event=exception_trace_install file_open=0");
    return;
  }
  g_exception_handler = AddVectoredExceptionHandler(1, trace_access_violation);
  append_line(std::string("event=exception_trace_install installed=") +
              (g_exception_handler ? "1" : "0"));
}

[[nodiscard]] std::wstring multiframe_log_path(std::uint32_t device_id,
                                               std::uint32_t generation) {
  wchar_t temp[MAX_PATH]{};
  const DWORD length = GetTempPathW(static_cast<DWORD>(std::size(temp)), temp);
  std::wostringstream out;
  if (length > 0 && length < std::size(temp))
    out << temp;
  out << L"ltr_d3d9_real_multiframe_" << GetCurrentProcessId() << L'_'
      << device_id << L'_' << generation << L".log";
  return out.str();
}

struct RealBridgeRuntime {
  std::mutex mutex;
  std::uint32_t device_id = 0;
  std::uint32_t generation = 0;
  UINT width = 0;
  UINT height = 0;
  D3DFORMAT format = D3DFMT_UNKNOWN;
  bool attempted = false;
  bool disabled = false;
  bool completion_logged = false;
  std::uint32_t capture_failures = 0;
  std::uint32_t backpressure_skips = 0;
  std::uint32_t submitted_frames = 0;
  std::uint64_t d3d9_capture_ticks = 0;
  std::uint64_t d3d11_submit_ticks = 0;
  LARGE_INTEGER performance_frequency{};
  std::array<ComPtr<IDirect3DTexture9>, ltr::d3d9_real_bridge::kRingDepth>
      relay9;
  std::array<ComPtr<IDirect3DSurface9>, ltr::d3d9_real_bridge::kRingDepth>
      relay_surface9;
  ComPtr<IDirect3DQuery9> completion_query9;
  ComPtr<ID3D11Device> d3d11;
  ComPtr<ID3D11DeviceContext> context11;
  std::array<ComPtr<ID3D11Texture2D>, ltr::d3d9_real_bridge::kRingDepth>
      relay11;
  ltr::d3d9_real_bridge::Client client;

  RealBridgeRuntime(std::uint32_t id, std::uint32_t resource_generation)
      : device_id(id), generation(resource_generation) {
    QueryPerformanceFrequency(&performance_frequency);
  }

  [[nodiscard]] double MeanMicroseconds(std::uint64_t ticks) const {
    if (!submitted_frames || !performance_frequency.QuadPart)
      return 0.0;
    return static_cast<double>(ticks) * 1'000'000.0 /
           static_cast<double>(performance_frequency.QuadPart) /
           static_cast<double>(submitted_frames);
  }

  void Disable(const char *stage, HRESULT hr = E_FAIL) {
    disabled = true;
    std::ostringstream out;
    out << "event=real_multiframe_bridge stage=" << stage
        << " device_id=" << device_id << " generation=" << generation
        << " hr=0x" << std::hex << static_cast<unsigned long>(hr) << std::dec
        << " fail_open=1 RESULT FAIL";
    append_line(out.str());
    client.Shutdown(true);
    relay11 = {};
    context11.Reset();
    d3d11.Reset();
    completion_query9.Reset();
    relay_surface9 = {};
    relay9 = {};
  }

  [[nodiscard]] bool Initialize(IDirect3DDevice9 *device,
                                const D3DSURFACE_DESC &source_desc) {
    attempted = true;
    if (source_desc.Format != D3DFMT_A8R8G8B8) {
      Disable("unsupported_source_format", D3DERR_INVALIDCALL);
      return false;
    }
    width = source_desc.Width;
    height = source_desc.Height;
    format = source_desc.Format;

    std::array<HANDLE, ltr::d3d9_real_bridge::kRingDepth> relay_handles{};
    HRESULT hr = S_OK;
    for (std::uint32_t slot = 0; slot < ltr::d3d9_real_bridge::kRingDepth;
         ++slot) {
      hr = device->CreateTexture(width, height, 1, 0, format, D3DPOOL_DEFAULT,
                                 &relay9[slot], &relay_handles[slot]);
      if (SUCCEEDED(hr) && relay9[slot])
        hr = relay9[slot]->GetSurfaceLevel(0, &relay_surface9[slot]);
      if (FAILED(hr) || !relay_handles[slot] || !relay_surface9[slot])
        break;
    }
    if (SUCCEEDED(hr))
      hr = device->CreateQuery(D3DQUERYTYPE_EVENT, &completion_query9);
    if (FAILED(hr) || !completion_query9) {
      Disable("d3d9_relay", hr);
      return false;
    }

    D3D_FEATURE_LEVEL feature_level{};
    hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
                           D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0,
                           D3D11_SDK_VERSION, &d3d11, &feature_level,
                           &context11);
    for (std::uint32_t slot = 0;
         SUCCEEDED(hr) && slot < ltr::d3d9_real_bridge::kRingDepth; ++slot) {
      hr = d3d11->OpenSharedResource(relay_handles[slot],
                                     IID_PPV_ARGS(&relay11[slot]));
    }
    if (FAILED(hr)) {
      Disable("d3d11_relay", hr);
      return false;
    }

    for (const auto &relay : relay11) {
      D3D11_TEXTURE2D_DESC relay_desc{};
      relay->GetDesc(&relay_desc);
      if (relay_desc.Width != width || relay_desc.Height != height ||
          relay_desc.Format != DXGI_FORMAT_B8G8R8A8_UNORM ||
          relay_desc.SampleDesc.Count != 1) {
        Disable("d3d11_relay_contract", E_INVALIDARG);
        return false;
      }
    }

    ltr::d3d9_real_bridge::ClientOptions options{};
    options.sink_path = x64_bridge_sink_path();
    options.log_path = multiframe_log_path(device_id, generation);
    options.width = width;
    options.height = height;
    options.frames = ltr::d3d9_real_bridge::kDefaultFrames;
    options.generation = generation;
    options.initial_stall_ms = generation == 1 ? multiframe_initial_stall_ms() : 0;
    options.validate_synthetic_pattern = false;
    if (!client.Start(d3d11.Get(), context11.Get(), options,
                      [](const std::string &line) { append_line(line); })) {
      Disable("transport_start");
      return false;
    }

    std::ostringstream out;
    out << "event=real_multiframe_bridge stage=initialized device_id="
        << device_id << " generation=" << generation << " width=" << width
        << " height=" << height << " format="
        << static_cast<unsigned>(format) << " ring_depth="
        << ltr::d3d9_real_bridge::kRingDepth << " frames="
        << ltr::d3d9_real_bridge::kDefaultFrames
        << " initial_stall_ms=" << options.initial_stall_ms
        << " validation_readback=0 source_reuse=done_fence";
    append_line(out.str());
    return true;
  }

  [[nodiscard]] HRESULT WaitForD3D9Completion() {
    HRESULT hr = completion_query9->Issue(D3DISSUE_END);
    if (FAILED(hr))
      return hr;
    for (std::uint32_t attempt = 0; attempt < 10000; ++attempt) {
      hr = completion_query9->GetData(nullptr, 0, D3DGETDATA_FLUSH);
      if (hr == S_OK)
        return S_OK;
      if (FAILED(hr))
        return hr;
      Sleep(0);
    }
    return HRESULT_FROM_WIN32(WAIT_TIMEOUT);
  }

  void Capture(IDirect3DDevice9 *device) {
    std::lock_guard lock(mutex);
    if (disabled || completion_logged)
      return;

    ComPtr<IDirect3DSurface9> source;
    D3DSURFACE_DESC source_desc{};
    HRESULT hr = device->GetRenderTarget(0, &source);
    if (SUCCEEDED(hr) && source)
      hr = source->GetDesc(&source_desc);
    if (FAILED(hr) || !source) {
      if (++capture_failures <= 3)
        append_line("event=real_multiframe_bridge stage=source_unavailable fail_open=1");
      return;
    }

    if (!attempted && !Initialize(device, source_desc))
      return;
    if (disabled)
      return;
    if (source_desc.Width != width || source_desc.Height != height ||
        source_desc.Format != format) {
      Disable("unexpected_resource_change", D3DERR_INVALIDCALL);
      return;
    }

    const auto availability = client.QuerySubmitStatus();
    if (availability == ltr::d3d9_real_bridge::SubmitStatus::backpressure) {
      ++backpressure_skips;
      if (backpressure_skips <= 3) {
        const auto snapshot = client.Snapshot();
        std::ostringstream out;
        out << "event=real_multiframe_bridge stage=backpressure device_id="
            << device_id << " generation=" << generation
            << " submitted=" << snapshot.submitted
            << " done_completed=" << snapshot.done_completed
            << " skipped=" << backpressure_skips;
        append_line(out.str());
      }
      return;
    }
    if (availability ==
        ltr::d3d9_real_bridge::SubmitStatus::waiting_for_completion) {
      (void)client.Poll();
      return;
    }
    if (availability == ltr::d3d9_real_bridge::SubmitStatus::finished) {
      completion_logged = true;
      client.Shutdown(false);
      relay11 = {};
      context11.Reset();
      d3d11.Reset();
      completion_query9.Reset();
      relay_surface9 = {};
      relay9 = {};
      std::ostringstream out;
      out << "event=real_multiframe_bridge stage=completed device_id="
          << device_id << " generation=" << generation
          << " backpressure_skips=" << backpressure_skips
          << " capture_failures=" << capture_failures
          << " d3d9_copy_wait_mean_us="
          << MeanMicroseconds(d3d9_capture_ticks)
          << " d3d11_submit_cpu_mean_us="
          << MeanMicroseconds(d3d11_submit_ticks) << " RESULT PASS";
      append_line(out.str());
      return;
    }
    if (availability == ltr::d3d9_real_bridge::SubmitStatus::failed) {
      Disable("transport_failure");
      return;
    }

    // QuerySubmitStatus only returns submitted when the previous use of this
    // ring slot has reached D3D12's done fence, which is downstream of ready
    // and therefore of the D3D11 copy that reads this relay.
    const std::uint32_t slot =
        client.Snapshot().submitted % ltr::d3d9_real_bridge::kRingDepth;
    LARGE_INTEGER d3d9_begin{};
    LARGE_INTEGER d3d9_end{};
    QueryPerformanceCounter(&d3d9_begin);
    hr = device->StretchRect(source.Get(), nullptr, relay_surface9[slot].Get(),
                             nullptr, D3DTEXF_NONE);
    if (SUCCEEDED(hr))
      hr = WaitForD3D9Completion();
    QueryPerformanceCounter(&d3d9_end);
    if (FAILED(hr)) {
      if (++capture_failures <= 3) {
        const double wait_us = performance_frequency.QuadPart
                                   ? static_cast<double>(d3d9_end.QuadPart -
                                                         d3d9_begin.QuadPart) *
                                         1'000'000.0 /
                                         static_cast<double>(
                                             performance_frequency.QuadPart)
                                   : 0.0;
        std::ostringstream out;
        out << "event=real_multiframe_bridge stage=capture_sync hr=0x"
            << std::hex << static_cast<unsigned long>(hr) << std::dec
            << " submitted=" << submitted_frames << " slot=" << slot
            << " wait_us=" << wait_us
            << " fail_open=1";
        append_line(out.str());
      }
      return;
    }

    LARGE_INTEGER d3d11_begin{};
    LARGE_INTEGER d3d11_end{};
    QueryPerformanceCounter(&d3d11_begin);
    const auto submitted = client.TrySubmit(relay11[slot].Get());
    QueryPerformanceCounter(&d3d11_end);
    if (submitted == ltr::d3d9_real_bridge::SubmitStatus::submitted) {
      d3d9_capture_ticks += static_cast<std::uint64_t>(
          d3d9_end.QuadPart - d3d9_begin.QuadPart);
      d3d11_submit_ticks += static_cast<std::uint64_t>(
          d3d11_end.QuadPart - d3d11_begin.QuadPart);
      ++submitted_frames;
    } else if (submitted == ltr::d3d9_real_bridge::SubmitStatus::failed) {
      Disable("transport_submit");
    }
  }

  void Shutdown(const char *reason, bool expected_cancellation = false) {
    std::lock_guard lock(mutex);
    if (completion_logged)
      return;
    const auto snapshot = client.Snapshot();
    std::ostringstream out;
    out << "event=real_multiframe_bridge stage=shutdown reason=" << reason
        << " device_id=" << device_id << " generation=" << generation
        << " submitted=" << snapshot.submitted
        << " done_completed=" << snapshot.done_completed
        << " fail_open=1";
    append_line(out.str());
    if (expected_cancellation)
      client.Cancel();
    else
      client.Shutdown(true);
    relay11 = {};
    context11.Reset();
    d3d11.Reset();
    completion_query9.Reset();
    relay_surface9 = {};
    relay9 = {};
    disabled = true;
  }
};

template <typename Fn>
[[nodiscard]] bool patch_vtable(void *object, std::size_t index, Fn hook,
                                 Fn &original) {
  auto **vtable = *reinterpret_cast<void ***>(object);
  void *const hook_ptr = reinterpret_cast<void *>(hook);
  if (vtable[index] == hook_ptr)
    return true;
  DWORD old_protect = 0;
  if (!VirtualProtect(&vtable[index], sizeof(void *), PAGE_EXECUTE_READWRITE,
                      &old_protect))
    return false;
  if (!original)
    original = reinterpret_cast<Fn>(vtable[index]);
  vtable[index] = hook_ptr;
  DWORD ignored = 0;
  VirtualProtect(&vtable[index], sizeof(void *), old_protect, &ignored);
  FlushInstructionCache(GetCurrentProcess(), &vtable[index], sizeof(void *));
  return original != nullptr;
}

struct ManagedDeviceIdentity {
  std::uint32_t id = 0;
  std::uint32_t generation = 0;
};

[[nodiscard]] ManagedDeviceIdentity
managed_device_identity(IDirect3DDevice9 *device) {
  if (!device)
    return {};
  std::lock_guard lock(g_state_mutex);
  const auto it = g_devices.find(device);
  if (it == g_devices.end())
    return {};
  return ManagedDeviceIdentity{it->second.id, it->second.resource_generation};
}

[[nodiscard]] bool install_managed_resource_release_trace(void *resource) {
  if (!resource)
    return false;
  auto **vtable = *reinterpret_cast<void ***>(resource);
  if (!vtable)
    return false;

  const void *const hook =
      reinterpret_cast<void *>(&hook_managed_resource_release);
  std::lock_guard lock(g_managed_resource_mutex);
  const auto existing = g_managed_resource_release_originals.find(vtable);
  if (existing != g_managed_resource_release_originals.end()) {
    if (vtable[kResourceReleaseIndex] == hook)
      return true;
  } else if (vtable[kResourceReleaseIndex] == hook) {
    return false;
  }

  auto original = existing != g_managed_resource_release_originals.end()
                      ? existing->second
                      : reinterpret_cast<ResourceReleaseFn>(
                            vtable[kResourceReleaseIndex]);
  if (!original)
    return false;

  DWORD old_protect = 0;
  if (!VirtualProtect(&vtable[kResourceReleaseIndex], sizeof(void *),
                      PAGE_EXECUTE_READWRITE, &old_protect))
    return false;
  vtable[kResourceReleaseIndex] =
      reinterpret_cast<void *>(&hook_managed_resource_release);
  DWORD ignored = 0;
  VirtualProtect(&vtable[kResourceReleaseIndex], sizeof(void *), old_protect,
                 &ignored);
  FlushInstructionCache(GetCurrentProcess(), &vtable[kResourceReleaseIndex],
                        sizeof(void *));
  g_managed_resource_release_originals[vtable] = original;
  return true;
}

[[nodiscard]] std::uint32_t track_managed_resource(
    IDirect3DDevice9 *device, void *resource, ManagedResourceKind kind) {
  if (!resource || !managed_resource_trace_requested())
    return 0;
  const ManagedDeviceIdentity device_identity = managed_device_identity(device);
  std::uint32_t id = 0;
  {
    std::lock_guard lock(g_managed_resource_mutex);
    id = g_next_managed_resource_id++;
    g_managed_resources[resource] = ManagedResourceTrace{
        id, kind, device, device_identity.id, device_identity.generation, 0, 0,
        0};
  }
  const bool release_hooked = install_managed_resource_release_trace(resource);
  std::ostringstream out;
  out << "event=managed_resource_lifetime action=track resource_id=" << id
      << " kind=" << managed_resource_kind_name(kind)
      << " resource=0x" << std::hex
      << reinterpret_cast<std::uintptr_t>(resource) << std::dec
      << " device_id=" << device_identity.id
      << " creation_generation=" << device_identity.generation
      << " release_hook=" << release_hooked;
  append_line(out.str());
  return id;
}

ULONG STDMETHODCALLTYPE hook_managed_resource_release(void *resource) {
  ResourceReleaseFn original = nullptr;
  ManagedResourceTrace trace{};
  bool tracked = false;
  std::uint32_t release_call = 0;
  const bool observer_draw_query_release = g_managed_draw_query_release;
  if (resource) {
    auto **vtable = *reinterpret_cast<void ***>(resource);
    std::lock_guard lock(g_managed_resource_mutex);
    if (vtable) {
      const auto original_it = g_managed_resource_release_originals.find(vtable);
      if (original_it != g_managed_resource_release_originals.end())
        original = original_it->second;
    }
    const auto trace_it = g_managed_resources.find(resource);
    if (!observer_draw_query_release && trace_it != g_managed_resources.end()) {
      tracked = true;
      release_call = ++trace_it->second.release_calls;
      trace = trace_it->second;
    }
  }

  if (!original) {
    if (tracked) {
      std::ostringstream out;
      out << "event=managed_resource_lifetime action=release_missing_original"
          << " resource_id=" << trace.id << " kind="
          << managed_resource_kind_name(trace.kind) << " resource=0x"
          << std::hex << reinterpret_cast<std::uintptr_t>(resource) << std::dec;
      append_line(out.str());
    }
    return 1;
  }

  const ULONG references = original(resource);
  if (tracked) {
    if (references == 0) {
      std::lock_guard lock(g_managed_resource_mutex);
      g_managed_resources.erase(resource);
      for (auto it = g_managed_surfaces.begin(); it != g_managed_surfaces.end();) {
        if (it->second.resource_id == trace.id)
          it = g_managed_surfaces.erase(it);
        else
          ++it;
      }
    }
    std::ostringstream out;
    out << "event=managed_resource_lifetime action=release resource_id="
        << trace.id << " kind=" << managed_resource_kind_name(trace.kind)
        << " resource=0x" << std::hex
        << reinterpret_cast<std::uintptr_t>(resource) << std::dec
        << " device_id=" << trace.device_id
        << " creation_generation=" << trace.creation_generation
        << " release_call=" << release_call << " references=" << references
        << " destroyed=" << (references == 0);
    append_line(out.str());
  }
  return references;
}

void write_managed_resource_snapshot(IDirect3DDevice9 *device,
                                     const char *phase, HRESULT reset_hr) {
  if (!managed_resource_trace_requested())
    return;
  const ManagedDeviceIdentity device_identity = managed_device_identity(device);
  std::ostringstream out;
  out << "event=managed_resource_reset_snapshot phase=" << phase
      << " device_id=" << device_identity.id
      << " current_generation=" << device_identity.generation
      << " reset_hr=0x" << std::hex << static_cast<unsigned long>(reset_hr)
      << std::dec;
  std::size_t active = 0;
  std::array<std::size_t, 4> by_kind{};
  {
    std::lock_guard lock(g_managed_resource_mutex);
    for (const auto &[resource, trace] : g_managed_resources) {
      if (trace.device != device)
        continue;
      ++active;
      const auto kind_index = static_cast<std::size_t>(trace.kind);
      if (kind_index < by_kind.size())
        ++by_kind[kind_index];
      if (active <= 64) {
        out << " r" << active << '=' << trace.id << ':'
            << managed_resource_kind_name(trace.kind) << ":0x" << std::hex
            << reinterpret_cast<std::uintptr_t>(resource) << std::dec << ":g"
            << trace.creation_generation << ":rel" << trace.release_calls;
      }
    }
  }
  out << " active=" << active << " texture2d=" << by_kind[0]
      << " cube_texture=" << by_kind[1] << " vertex_buffer=" << by_kind[2]
      << " index_buffer=" << by_kind[3];
  append_line(out.str());
}

[[nodiscard]] bool managed_texture_content_format_supported(D3DFORMAT format) {
  return format == D3DFMT_A8R8G8B8 || format == D3DFMT_X8R8G8B8 ||
         format == D3DFMT_A8B8G8R8 || format == D3DFMT_X8B8G8R8;
}

[[nodiscard]] ManagedTextureContentFingerprint
fingerprint_managed_texture(IDirect3DTexture9 *texture, HRESULT &desc_hr,
                            HRESULT &lock_hr, HRESULT &unlock_hr, INT &pitch) {
  ManagedTextureContentFingerprint fingerprint{};
  desc_hr = D3DERR_INVALIDCALL;
  lock_hr = D3DERR_INVALIDCALL;
  unlock_hr = D3DERR_INVALIDCALL;
  pitch = 0;
  if (!texture || !g_real_texture_lock_rect || !g_real_texture_unlock_rect)
    return fingerprint;

  D3DSURFACE_DESC desc{};
  desc_hr = texture->GetLevelDesc(0, &desc);
  if (FAILED(desc_hr))
    return fingerprint;
  fingerprint.width = desc.Width;
  fingerprint.height = desc.Height;
  fingerprint.format = desc.Format;
  if (!managed_texture_content_format_supported(desc.Format))
    return fingerprint;

  D3DLOCKED_RECT locked{};
  lock_hr = g_real_texture_lock_rect(texture, 0, &locked, nullptr,
                                     D3DLOCK_READONLY | D3DLOCK_NOSYSLOCK);
  if (FAILED(lock_hr) || !locked.pBits)
    return fingerprint;
  pitch = locked.Pitch;

  const std::uint64_t row_bytes = static_cast<std::uint64_t>(desc.Width) * 4u;
  if (locked.Pitch <= 0 ||
      static_cast<std::uint64_t>(locked.Pitch) < row_bytes) {
    unlock_hr = g_real_texture_unlock_rect(texture, 0);
    return fingerprint;
  }

  constexpr std::uint64_t kFnvOffsetBasis = 14695981039346656037ull;
  constexpr std::uint64_t kFnvPrime = 1099511628211ull;
  std::uint64_t hash = kFnvOffsetBasis;
  auto *row = static_cast<const std::uint8_t *>(locked.pBits);
  for (UINT y = 0; y < desc.Height; ++y) {
    for (std::uint64_t x = 0; x < row_bytes; ++x) {
      hash ^= row[x];
      hash *= kFnvPrime;
    }
    row += locked.Pitch;
  }
  unlock_hr = g_real_texture_unlock_rect(texture, 0);
  if (FAILED(unlock_hr))
    return fingerprint;

  fingerprint.hash = hash;
  fingerprint.bytes = row_bytes * desc.Height;
  fingerprint.valid = true;
  return fingerprint;
}

void probe_managed_texture_content_across_reset(IDirect3DDevice9 *device,
                                                const char *phase) {
  if (!device || !managed_reset_content_probe_requested())
    return;

  struct Candidate {
    IDirect3DTexture9 *texture = nullptr;
    ManagedResourceTrace trace{};
  };
  std::vector<Candidate> candidates;
  {
    std::lock_guard lock(g_managed_resource_mutex);
    for (const auto &[resource, trace] : g_managed_resources) {
      if (trace.device != device || trace.kind != ManagedResourceKind::texture2d)
        continue;
      auto *texture = static_cast<IDirect3DTexture9 *>(resource);
      texture->AddRef();
      candidates.push_back(Candidate{texture, trace});
    }
  }

  for (const Candidate &candidate : candidates) {
    HRESULT desc_hr = D3DERR_INVALIDCALL;
    HRESULT lock_hr = D3DERR_INVALIDCALL;
    HRESULT unlock_hr = D3DERR_INVALIDCALL;
    INT pitch = 0;
    const ManagedTextureContentFingerprint fingerprint =
        fingerprint_managed_texture(candidate.texture, desc_hr, lock_hr,
                                    unlock_hr, pitch);

    bool had_before = false;
    bool matches_before = false;
    if (std::strcmp(phase, "before") == 0) {
      g_managed_texture_pre_reset_fingerprints[candidate.trace.id] = fingerprint;
    } else {
      const auto before =
          g_managed_texture_pre_reset_fingerprints.find(candidate.trace.id);
      had_before = before != g_managed_texture_pre_reset_fingerprints.end() &&
                   before->second.valid;
      matches_before =
          had_before && fingerprint.valid &&
          before->second.width == fingerprint.width &&
          before->second.height == fingerprint.height &&
          before->second.format == fingerprint.format &&
          before->second.bytes == fingerprint.bytes &&
          before->second.hash == fingerprint.hash;
    }

    std::ostringstream out;
    out << "event=managed_reset_content phase=" << phase
        << " resource_id=" << candidate.trace.id
        << " creation_generation=" << candidate.trace.creation_generation
        << " desc_hr=0x" << std::hex << static_cast<unsigned long>(desc_hr)
        << " lock_hr=0x" << static_cast<unsigned long>(lock_hr)
        << " unlock_hr=0x" << static_cast<unsigned long>(unlock_hr) << std::dec
        << " width=" << fingerprint.width << " height=" << fingerprint.height
        << " format=" << static_cast<unsigned long>(fingerprint.format)
        << " pitch=" << pitch << " bytes=" << fingerprint.bytes << " hash=0x"
        << std::hex << fingerprint.hash << std::dec
        << " valid=" << fingerprint.valid;
    if (std::strcmp(phase, "before") != 0)
      out << " had_before=" << had_before
          << " matches_before=" << matches_before;
    append_line(out.str());

    const bool previous = g_managed_draw_query_release;
    g_managed_draw_query_release = true;
    candidate.texture->Release();
    g_managed_draw_query_release = previous;
  }
}

[[nodiscard]] std::uint32_t managed_resource_id(
    void *resource, ManagedResourceKind kind) {
  if (!resource || !managed_resource_trace_requested())
    return 0;
  std::lock_guard lock(g_managed_resource_mutex);
  const auto it = g_managed_resources.find(resource);
  if (it == g_managed_resources.end() || it->second.kind != kind)
    return 0;
  return it->second.id;
}

[[nodiscard]] bool take_stale_managed_binding_observation(
    IDirect3DDevice9 *device, void *resource, std::uint8_t binding_mask,
    ManagedResourceKind expected_kind, bool allow_cube_texture,
    ManagedResourceTrace &trace, std::uint32_t &current_generation) {
  if (!device || !resource || !managed_resource_trace_requested())
    return false;

  const ManagedDeviceIdentity identity = managed_device_identity(device);
  current_generation = identity.generation;
  if (identity.id == 0 || current_generation == 0)
    return false;

  std::lock_guard lock(g_managed_resource_mutex);
  const auto it = g_managed_resources.find(resource);
  if (it == g_managed_resources.end() || it->second.device != device)
    return false;
  if (it->second.kind != expected_kind &&
      !(allow_cube_texture &&
        it->second.kind == ManagedResourceKind::cube_texture))
    return false;
  if (!ltr::d3d9_real_observer::should_trace_stale_managed_binding(
          it->second.creation_generation, current_generation))
    return false;

  if (it->second.binding_generation != current_generation) {
    it->second.binding_generation = current_generation;
    it->second.binding_mask = 0;
  }
  if ((it->second.binding_mask & binding_mask) != 0)
    return false;

  it->second.binding_mask |= binding_mask;
  trace = it->second;
  return true;
}

void write_stale_managed_binding(const ManagedResourceTrace &trace,
                                 std::uint32_t current_generation,
                                 const char *binding, UINT slot,
                                 HRESULT hr) {
  std::ostringstream out;
  out << "event=managed_resource_binding resource_id=" << trace.id
      << " kind=" << managed_resource_kind_name(trace.kind)
      << " binding=" << binding << " slot=" << slot
      << " device_id=" << trace.device_id
      << " creation_generation=" << trace.creation_generation
      << " current_generation=" << current_generation << " hr=0x" << std::hex
      << static_cast<unsigned long>(hr) << std::dec;
  append_line(out.str());
}

[[nodiscard]] bool lookup_stale_managed_draw_resource(
    IDirect3DDevice9 *device, void *resource, ManagedResourceKind expected_kind,
    bool allow_cube_texture, ManagedResourceTrace &trace,
    std::uint32_t current_generation) {
  if (!device || !resource || !managed_resource_trace_requested() ||
      current_generation == 0)
    return false;

  std::lock_guard lock(g_managed_resource_mutex);
  const auto it = g_managed_resources.find(resource);
  if (it == g_managed_resources.end() || it->second.device != device)
    return false;
  if (it->second.kind != expected_kind &&
      !(allow_cube_texture &&
        it->second.kind == ManagedResourceKind::cube_texture))
    return false;
  if (!ltr::d3d9_real_observer::should_trace_stale_managed_binding(
          it->second.creation_generation, current_generation))
    return false;

  trace = it->second;
  return true;
}

template <typename T> void release_managed_draw_query_reference(T *resource) {
  if (!resource)
    return;
  const bool previous = g_managed_draw_query_release;
  g_managed_draw_query_release = true;
  resource->Release();
  g_managed_draw_query_release = previous;
}

struct ManagedDrawScanTicket {
  std::uint32_t device_id = 0;
  std::uint32_t generation = 0;
  std::uint32_t sample = 0;
};

[[nodiscard]] bool acquire_managed_draw_scan_ticket(
    IDirect3DDevice9 *device, ManagedDrawScanTicket &ticket) {
  if (!device || !managed_resource_trace_requested())
    return false;

  std::lock_guard lock(g_state_mutex);
  const auto it = g_devices.find(device);
  if (it == g_devices.end())
    return false;
  DeviceState &state = it->second;
  if (state.managed_draw_scan_generation != state.resource_generation) {
    state.managed_draw_scan_generation = state.resource_generation;
    state.managed_draw_scan_samples = 0;
    state.managed_draw_scan_stale_bindings = 0;
    state.managed_draw_scan_query_failures = 0;
  }
  if (!ltr::d3d9_real_observer::should_sample_post_reset_draw_state(
          state.resource_generation, state.managed_draw_scan_samples,
          kManagedDrawStateSampleLimit))
    return false;

  ++state.managed_draw_scan_samples;
  ticket = ManagedDrawScanTicket{state.id, state.resource_generation,
                                 state.managed_draw_scan_samples};
  return true;
}

void write_stale_managed_draw_binding(const ManagedResourceTrace &trace,
                                      const ManagedDrawScanTicket &ticket,
                                      const char *draw, const char *binding,
                                      UINT slot) {
  const LONG ordinal =
      InterlockedIncrement(&g_managed_draw_binding_trace_count);
  if (ordinal > 32)
    return;
  std::ostringstream out;
  out << "event=managed_draw_state_active ordinal=" << ordinal
      << " device_id=" << ticket.device_id
      << " current_generation=" << ticket.generation
      << " sample=" << ticket.sample << " draw=" << draw
      << " resource_id=" << trace.id
      << " kind=" << managed_resource_kind_name(trace.kind)
      << " creation_generation=" << trace.creation_generation
      << " binding=" << binding << " slot=" << slot;
  append_line(out.str());
}

void finish_managed_draw_scan_sample(const ManagedDrawScanTicket &ticket,
                                     const char *draw,
                                     std::uint32_t stale_bindings,
                                     std::uint32_t query_failures) {
  std::uint64_t total_stale_bindings = 0;
  std::uint64_t total_query_failures = 0;
  bool emit = false;
  bool complete = false;
  {
    std::lock_guard lock(g_state_mutex);
    for (auto &[device, state] : g_devices) {
      if (state.id != ticket.device_id ||
          state.resource_generation != ticket.generation)
        continue;
      state.managed_draw_scan_stale_bindings += stale_bindings;
      state.managed_draw_scan_query_failures += query_failures;
      total_stale_bindings = state.managed_draw_scan_stale_bindings;
      total_query_failures = state.managed_draw_scan_query_failures;
      complete = state.managed_draw_scan_samples >= kManagedDrawStateSampleLimit;
      emit = ticket.sample == 1 || complete || stale_bindings != 0 ||
             query_failures != 0;
      break;
    }
  }
  if (!emit)
    return;

  std::ostringstream out;
  out << "event=managed_draw_state_scan device_id=" << ticket.device_id
      << " current_generation=" << ticket.generation
      << " sample=" << ticket.sample << " draw=" << draw
      << " stale_bindings=" << stale_bindings
      << " query_failures=" << query_failures
      << " total_stale_bindings=" << total_stale_bindings
      << " total_query_failures=" << total_query_failures
      << " complete=" << complete;
  append_line(out.str());
}

void observe_managed_draw_state(IDirect3DDevice9 *device, const char *draw) {
  ManagedDrawScanTicket ticket{};
  if (!acquire_managed_draw_scan_ticket(device, ticket))
    return;

  std::uint32_t stale_bindings = 0;
  std::uint32_t query_failures = 0;

  auto observe_texture = [&](DWORD stage) {
    IDirect3DBaseTexture9 *texture = nullptr;
    const HRESULT hr = device->GetTexture(stage, &texture);
    if (FAILED(hr)) {
      ++query_failures;
      return;
    }
    if (texture) {
      ManagedResourceTrace trace{};
      if (lookup_stale_managed_draw_resource(
              device, texture, ManagedResourceKind::texture2d, true, trace,
              ticket.generation)) {
        ++stale_bindings;
        write_stale_managed_draw_binding(trace, ticket, draw, "texture",
                                         static_cast<UINT>(stage));
      }
      release_managed_draw_query_reference(texture);
    }
  };

  for (DWORD stage = 0; stage < 16; ++stage)
    observe_texture(stage);
  for (DWORD stage = D3DVERTEXTEXTURESAMPLER0;
       stage <= D3DVERTEXTEXTURESAMPLER3; ++stage)
    observe_texture(stage);

  for (UINT stream = 0; stream < 4; ++stream) {
    IDirect3DVertexBuffer9 *buffer = nullptr;
    UINT offset = 0;
    UINT stride = 0;
    const HRESULT hr = device->GetStreamSource(stream, &buffer, &offset, &stride);
    if (FAILED(hr)) {
      ++query_failures;
      continue;
    }
    if (buffer) {
      ManagedResourceTrace trace{};
      if (lookup_stale_managed_draw_resource(
              device, buffer, ManagedResourceKind::vertex_buffer, false, trace,
              ticket.generation)) {
        ++stale_bindings;
        write_stale_managed_draw_binding(trace, ticket, draw, "stream_source",
                                         stream);
      }
      release_managed_draw_query_reference(buffer);
    }
  }

  IDirect3DIndexBuffer9 *indices = nullptr;
  const HRESULT indices_hr = device->GetIndices(&indices);
  if (FAILED(indices_hr)) {
    ++query_failures;
  } else if (indices) {
    ManagedResourceTrace trace{};
    if (lookup_stale_managed_draw_resource(
            device, indices, ManagedResourceKind::index_buffer, false, trace,
            ticket.generation)) {
      ++stale_bindings;
      write_stale_managed_draw_binding(trace, ticket, draw, "indices", 0);
    }
    release_managed_draw_query_reference(indices);
  }

  finish_managed_draw_scan_sample(ticket, draw, stale_bindings, query_failures);
}

void track_managed_surface(IDirect3DSurface9 *surface, std::uint32_t resource_id,
                           ManagedResourceKind parent_kind,
                           D3DCUBEMAP_FACES face, UINT level) {
  if (!surface || resource_id == 0 || !managed_resource_trace_requested())
    return;
  std::lock_guard lock(g_managed_resource_mutex);
  g_managed_surfaces[surface] = ManagedSurfaceTrace{resource_id, parent_kind,
                                                    face, level};
}

[[nodiscard]] bool managed_surface_trace(IDirect3DSurface9 *surface,
                                         ManagedSurfaceTrace &trace) {
  if (!surface || !managed_resource_trace_requested())
    return false;
  std::lock_guard lock(g_managed_resource_mutex);
  const auto it = g_managed_surfaces.find(surface);
  if (it == g_managed_surfaces.end())
    return false;
  trace = it->second;
  return true;
}

[[nodiscard]] bool surface_vtable_originals(
    IDirect3DSurface9 *surface, SurfaceVtableOriginals &originals) {
  if (!surface)
    return false;
  auto **vtable = *reinterpret_cast<void ***>(surface);
  if (!vtable)
    return false;
  std::lock_guard lock(g_managed_resource_mutex);
  const auto it = g_surface_vtable_originals.find(vtable);
  if (it == g_surface_vtable_originals.end())
    return false;
  originals = it->second;
  return originals.lock_rect && originals.unlock_rect;
}

[[nodiscard]] bool install_surface_vtable_hooks(IDirect3DSurface9 *surface) {
  if (!surface)
    return false;
  auto **vtable = *reinterpret_cast<void ***>(surface);
  if (!vtable)
    return false;

  const void *const lock_hook = reinterpret_cast<void *>(hook_surface_lock_rect);
  const void *const unlock_hook =
      reinterpret_cast<void *>(hook_surface_unlock_rect);

  std::lock_guard lock(g_managed_resource_mutex);
  auto it = g_surface_vtable_originals.find(vtable);
  SurfaceVtableOriginals originals{};
  if (it != g_surface_vtable_originals.end()) {
    originals = it->second;
  } else {
    if (vtable[kSurfaceLockRectIndex] == lock_hook ||
        vtable[kSurfaceUnlockRectIndex] == unlock_hook)
      return false;
    originals.lock_rect =
        reinterpret_cast<SurfaceLockRectFn>(vtable[kSurfaceLockRectIndex]);
    originals.unlock_rect =
        reinterpret_cast<SurfaceUnlockRectFn>(vtable[kSurfaceUnlockRectIndex]);
    if (!originals.lock_rect || !originals.unlock_rect)
      return false;
  }

  auto patch_slot = [&](std::size_t index, void *hook) {
    if (vtable[index] == hook)
      return true;
    DWORD old_protect = 0;
    if (!VirtualProtect(&vtable[index], sizeof(void *), PAGE_EXECUTE_READWRITE,
                        &old_protect))
      return false;
    vtable[index] = hook;
    DWORD ignored = 0;
    VirtualProtect(&vtable[index], sizeof(void *), old_protect, &ignored);
    FlushInstructionCache(GetCurrentProcess(), &vtable[index], sizeof(void *));
    return true;
  };

  if (!patch_slot(kSurfaceLockRectIndex,
                  reinterpret_cast<void *>(hook_surface_lock_rect)))
    return false;
  if (!patch_slot(kSurfaceUnlockRectIndex,
                  reinterpret_cast<void *>(hook_surface_unlock_rect))) {
    DWORD old_protect = 0;
    if (VirtualProtect(&vtable[kSurfaceLockRectIndex], sizeof(void *),
                       PAGE_EXECUTE_READWRITE, &old_protect)) {
      vtable[kSurfaceLockRectIndex] = reinterpret_cast<void *>(originals.lock_rect);
      DWORD ignored = 0;
      VirtualProtect(&vtable[kSurfaceLockRectIndex], sizeof(void *), old_protect,
                     &ignored);
      FlushInstructionCache(GetCurrentProcess(), &vtable[kSurfaceLockRectIndex],
                            sizeof(void *));
    }
    return false;
  }

  g_surface_vtable_originals[vtable] = originals;
  return true;
}

const char *managed_resource_kind_name(ManagedResourceKind kind) {
  switch (kind) {
  case ManagedResourceKind::texture2d:
    return "texture2d";
  case ManagedResourceKind::cube_texture:
    return "cube_texture";
  case ManagedResourceKind::vertex_buffer:
    return "vertex_buffer";
  case ManagedResourceKind::index_buffer:
    return "index_buffer";
  }
  return "unknown";
}

HRESULT STDMETHODCALLTYPE hook_surface_lock_rect(IDirect3DSurface9 *surface,
                                                  D3DLOCKED_RECT *locked_rect,
                                                  const RECT *rect,
                                                  DWORD flags) {
  SurfaceVtableOriginals originals{};
  if (!surface_vtable_originals(surface, originals) || !originals.lock_rect)
    return D3DERR_INVALIDCALL;
  const HRESULT hr = originals.lock_rect(surface, locked_rect, rect, flags);
  ManagedSurfaceTrace trace{};
  if (managed_surface_trace(surface, trace)) {
    const LONG ordinal =
        InterlockedIncrement(&g_managed_surface_lock_trace_count);
    if (ordinal <= 512) {
      std::ostringstream out;
      out << "event=managed_census_surface_lock ordinal=" << ordinal
          << " resource_id=" << trace.resource_id
          << " parent_kind=" << managed_resource_kind_name(trace.parent_kind)
          << " surface=0x" << std::hex
          << reinterpret_cast<std::uintptr_t>(surface) << std::dec
          << " face=" << static_cast<unsigned long>(trace.face)
          << " level=" << trace.level
          << " rect_present=" << (rect ? 1 : 0);
      if (rect)
        out << " left=" << rect->left << " top=" << rect->top
            << " right=" << rect->right << " bottom=" << rect->bottom;
      out << " flags=0x" << std::hex << flags << " hr=0x"
          << static_cast<unsigned long>(hr) << std::dec
          << " pitch=" << (locked_rect && SUCCEEDED(hr) ? locked_rect->Pitch : 0)
          << " data="
          << ((locked_rect && SUCCEEDED(hr) && locked_rect->pBits) ? "non_null"
                                                                  : "null");
      append_line(out.str());
    }
  }
  return hr;
}

HRESULT STDMETHODCALLTYPE hook_surface_unlock_rect(IDirect3DSurface9 *surface) {
  SurfaceVtableOriginals originals{};
  if (!surface_vtable_originals(surface, originals) || !originals.unlock_rect)
    return D3DERR_INVALIDCALL;
  const HRESULT hr = originals.unlock_rect(surface);
  ManagedSurfaceTrace trace{};
  if (managed_surface_trace(surface, trace)) {
    const LONG ordinal =
        InterlockedIncrement(&g_managed_surface_unlock_trace_count);
    if (ordinal <= 512) {
      std::ostringstream out;
      out << "event=managed_census_surface_unlock ordinal=" << ordinal
          << " resource_id=" << trace.resource_id
          << " parent_kind=" << managed_resource_kind_name(trace.parent_kind)
          << " surface=0x" << std::hex
          << reinterpret_cast<std::uintptr_t>(surface) << " hr=0x"
          << static_cast<unsigned long>(hr) << std::dec
          << " face=" << static_cast<unsigned long>(trace.face)
          << " level=" << trace.level;
      append_line(out.str());
    }
  }
  return hr;
}

void install_managed_surface_use_trace(IDirect3DSurface9 *surface,
                                       std::uint32_t resource_id,
                                       ManagedResourceKind parent_kind,
                                       D3DCUBEMAP_FACES face, UINT level) {
  if (!surface || resource_id == 0)
    return;
  track_managed_surface(surface, resource_id, parent_kind, face, level);
  const bool hooks_installed = install_surface_vtable_hooks(surface);
  std::ostringstream out;
  out << "event=managed_census_surface_use_trace resource_id=" << resource_id
      << " parent_kind=" << managed_resource_kind_name(parent_kind)
      << " surface=0x" << std::hex << reinterpret_cast<std::uintptr_t>(surface)
      << std::dec << " face=" << static_cast<unsigned long>(face)
      << " level=" << level << " lock_hook=" << hooks_installed
      << " unlock_hook=" << hooks_installed;
  append_line(out.str());
}

HRESULT STDMETHODCALLTYPE hook_texture_get_surface_level(
    IDirect3DTexture9 *texture, UINT level, IDirect3DSurface9 **surface) {
  const HRESULT hr = g_real_texture_get_surface_level(texture, level, surface);
  const std::uint32_t resource_id =
      managed_resource_id(texture, ManagedResourceKind::texture2d);
  if (resource_id != 0 && SUCCEEDED(hr) && surface && *surface) {
    install_managed_surface_use_trace(*surface, resource_id,
                                      ManagedResourceKind::texture2d,
                                      D3DCUBEMAP_FACE_POSITIVE_X, level);
  }
  return hr;
}

HRESULT STDMETHODCALLTYPE hook_cube_texture_get_cube_map_surface(
    IDirect3DCubeTexture9 *texture, D3DCUBEMAP_FACES face, UINT level,
    IDirect3DSurface9 **surface) {
  const HRESULT hr =
      g_real_cube_texture_get_cube_map_surface(texture, face, level, surface);
  const std::uint32_t resource_id =
      managed_resource_id(texture, ManagedResourceKind::cube_texture);
  if (resource_id != 0 && SUCCEEDED(hr) && surface && *surface) {
    install_managed_surface_use_trace(*surface, resource_id,
                                      ManagedResourceKind::cube_texture, face,
                                      level);
  }
  return hr;
}

[[nodiscard]] std::uintptr_t
current_device_vertex_buffer_slot(IDirect3DDevice9 *device) {
  if (!device)
    return 0;
  auto **vtable = *reinterpret_cast<void ***>(device);
  return vtable ? reinterpret_cast<std::uintptr_t>(
                      vtable[kDeviceCreateVertexBufferIndex])
                : 0;
}

HRESULT STDMETHODCALLTYPE hook_texture_lock_rect(
    IDirect3DTexture9 *texture, UINT level, D3DLOCKED_RECT *locked_rect,
    const RECT *rect, DWORD flags) {
  const HRESULT hr =
      g_real_texture_lock_rect(texture, level, locked_rect, rect, flags);
  const std::uint32_t resource_id =
      managed_resource_id(texture, ManagedResourceKind::texture2d);
  if (resource_id != 0) {
    const LONG ordinal =
        InterlockedIncrement(&g_managed_texture_lock_trace_count);
    if (ordinal <= 256) {
      std::ostringstream out;
      out << "event=managed_census_texture_lock ordinal=" << ordinal
          << " resource_id=" << resource_id << " texture=0x" << std::hex
          << reinterpret_cast<std::uintptr_t>(texture) << std::dec
          << " level=" << level << " rect_present=" << (rect ? 1 : 0);
      if (rect)
        out << " left=" << rect->left << " top=" << rect->top
            << " right=" << rect->right << " bottom=" << rect->bottom;
      out << " flags=0x" << std::hex << flags << " hr=0x"
          << static_cast<unsigned long>(hr) << std::dec
          << " pitch=" << (locked_rect && SUCCEEDED(hr) ? locked_rect->Pitch : 0)
          << " data="
          << ((locked_rect && SUCCEEDED(hr) && locked_rect->pBits) ? "non_null"
                                                                  : "null");
      append_line(out.str());
    }
  }
  return hr;
}

HRESULT STDMETHODCALLTYPE hook_texture_unlock_rect(IDirect3DTexture9 *texture,
                                                    UINT level) {
  const HRESULT hr = g_real_texture_unlock_rect(texture, level);
  const std::uint32_t resource_id =
      managed_resource_id(texture, ManagedResourceKind::texture2d);
  if (resource_id != 0) {
    const LONG ordinal =
        InterlockedIncrement(&g_managed_texture_unlock_trace_count);
    if (ordinal <= 256) {
      std::ostringstream out;
      out << "event=managed_census_texture_unlock ordinal=" << ordinal
          << " resource_id=" << resource_id << " texture=0x" << std::hex
          << reinterpret_cast<std::uintptr_t>(texture) << " hr=0x"
          << static_cast<unsigned long>(hr) << std::dec << " level=" << level;
      append_line(out.str());
    }
  }
  return hr;
}

HRESULT STDMETHODCALLTYPE hook_cube_texture_lock_rect(
    IDirect3DCubeTexture9 *texture, D3DCUBEMAP_FACES face, UINT level,
    D3DLOCKED_RECT *locked_rect, const RECT *rect, DWORD flags) {
  const HRESULT hr = g_real_cube_texture_lock_rect(texture, face, level,
                                                    locked_rect, rect, flags);
  const std::uint32_t resource_id =
      managed_resource_id(texture, ManagedResourceKind::cube_texture);
  if (resource_id != 0) {
    const LONG ordinal = InterlockedIncrement(&g_managed_cube_lock_trace_count);
    if (ordinal <= 256) {
      std::ostringstream out;
      out << "event=managed_census_cube_lock ordinal=" << ordinal
          << " resource_id=" << resource_id << " texture=0x" << std::hex
          << reinterpret_cast<std::uintptr_t>(texture) << std::dec
          << " face=" << static_cast<unsigned long>(face)
          << " level=" << level << " rect_present=" << (rect ? 1 : 0);
      if (rect)
        out << " left=" << rect->left << " top=" << rect->top
            << " right=" << rect->right << " bottom=" << rect->bottom;
      out << " flags=0x" << std::hex << flags << " hr=0x"
          << static_cast<unsigned long>(hr) << std::dec
          << " pitch=" << (locked_rect && SUCCEEDED(hr) ? locked_rect->Pitch : 0)
          << " data="
          << ((locked_rect && SUCCEEDED(hr) && locked_rect->pBits) ? "non_null"
                                                                  : "null");
      append_line(out.str());
    }
  }
  return hr;
}

HRESULT STDMETHODCALLTYPE hook_cube_texture_unlock_rect(
    IDirect3DCubeTexture9 *texture, D3DCUBEMAP_FACES face, UINT level) {
  const HRESULT hr = g_real_cube_texture_unlock_rect(texture, face, level);
  const std::uint32_t resource_id =
      managed_resource_id(texture, ManagedResourceKind::cube_texture);
  if (resource_id != 0) {
    const LONG ordinal =
        InterlockedIncrement(&g_managed_cube_unlock_trace_count);
    if (ordinal <= 256) {
      std::ostringstream out;
      out << "event=managed_census_cube_unlock ordinal=" << ordinal
          << " resource_id=" << resource_id << " texture=0x" << std::hex
          << reinterpret_cast<std::uintptr_t>(texture) << " hr=0x"
          << static_cast<unsigned long>(hr) << std::dec
          << " face=" << static_cast<unsigned long>(face)
          << " level=" << level;
      append_line(out.str());
    }
  }
  return hr;
}

HRESULT STDMETHODCALLTYPE hook_vertex_buffer_lock(IDirect3DVertexBuffer9 *buffer,
                                                  UINT offset_to_lock,
                                                  UINT size_to_lock,
                                                  void **data, DWORD flags) {
  const HRESULT hr = g_real_vertex_buffer_lock(
      buffer, offset_to_lock, size_to_lock, data, flags);
  const LONG ordinal = InterlockedIncrement(&g_vertex_buffer_lock_trace_count);
  if (ordinal <= 64) {
    std::ostringstream out;
    out << "event=managed_vertex_buffer_lock ordinal=" << ordinal
        << " buffer=0x" << std::hex << reinterpret_cast<std::uintptr_t>(buffer)
        << std::dec << " offset=" << offset_to_lock << " size=" << size_to_lock
        << " flags=0x" << std::hex << flags
        << " hr=0x" << static_cast<unsigned long>(hr) << std::dec
        << " data=" << ((data && *data) ? "non_null" : "null");
    append_line(out.str());
  }
  const std::uint32_t resource_id =
      managed_resource_id(buffer, ManagedResourceKind::vertex_buffer);
  if (resource_id != 0) {
    std::ostringstream out;
    out << "event=managed_census_vertex_buffer_lock resource_id=" << resource_id
        << " buffer=0x" << std::hex << reinterpret_cast<std::uintptr_t>(buffer)
        << std::dec << " offset=" << offset_to_lock << " size=" << size_to_lock
        << " flags=0x" << std::hex << flags << " hr=0x"
        << static_cast<unsigned long>(hr) << std::dec
        << " data=" << ((data && *data) ? "non_null" : "null");
    append_line(out.str());
  }
  return hr;
}

HRESULT STDMETHODCALLTYPE hook_vertex_buffer_unlock(IDirect3DVertexBuffer9 *buffer) {
  const HRESULT hr = g_real_vertex_buffer_unlock(buffer);
  const LONG ordinal = InterlockedIncrement(&g_vertex_buffer_unlock_trace_count);
  if (ordinal <= 64) {
    std::ostringstream out;
    out << "event=managed_vertex_buffer_unlock ordinal=" << ordinal
        << " buffer=0x" << std::hex << reinterpret_cast<std::uintptr_t>(buffer)
        << " hr=0x" << static_cast<unsigned long>(hr) << std::dec;
    append_line(out.str());
  }
  const std::uint32_t resource_id =
      managed_resource_id(buffer, ManagedResourceKind::vertex_buffer);
  if (resource_id != 0) {
    std::ostringstream out;
    out << "event=managed_census_vertex_buffer_unlock resource_id=" << resource_id
        << " buffer=0x" << std::hex << reinterpret_cast<std::uintptr_t>(buffer)
        << " hr=0x" << static_cast<unsigned long>(hr) << std::dec;
    append_line(out.str());
  }
  return hr;
}

HRESULT STDMETHODCALLTYPE hook_index_buffer_lock(IDirect3DIndexBuffer9 *buffer,
                                                  UINT offset_to_lock,
                                                  UINT size_to_lock, void **data,
                                                  DWORD flags) {
  const HRESULT hr = g_real_index_buffer_lock(buffer, offset_to_lock, size_to_lock,
                                               data, flags);
  const std::uint32_t resource_id =
      managed_resource_id(buffer, ManagedResourceKind::index_buffer);
  if (resource_id != 0) {
    const LONG ordinal =
        InterlockedIncrement(&g_managed_index_buffer_lock_trace_count);
    if (ordinal <= 256) {
      std::ostringstream out;
      out << "event=managed_census_index_buffer_lock ordinal=" << ordinal
          << " resource_id=" << resource_id << " buffer=0x" << std::hex
          << reinterpret_cast<std::uintptr_t>(buffer) << std::dec
          << " offset=" << offset_to_lock << " size=" << size_to_lock
          << " flags=0x" << std::hex << flags << " hr=0x"
          << static_cast<unsigned long>(hr) << std::dec
          << " data=" << ((data && *data) ? "non_null" : "null");
      append_line(out.str());
    }
  }
  return hr;
}

HRESULT STDMETHODCALLTYPE hook_index_buffer_unlock(IDirect3DIndexBuffer9 *buffer) {
  const HRESULT hr = g_real_index_buffer_unlock(buffer);
  const std::uint32_t resource_id =
      managed_resource_id(buffer, ManagedResourceKind::index_buffer);
  if (resource_id != 0) {
    const LONG ordinal =
        InterlockedIncrement(&g_managed_index_buffer_unlock_trace_count);
    if (ordinal <= 256) {
      std::ostringstream out;
      out << "event=managed_census_index_buffer_unlock ordinal=" << ordinal
          << " resource_id=" << resource_id << " buffer=0x" << std::hex
          << reinterpret_cast<std::uintptr_t>(buffer) << " hr=0x"
          << static_cast<unsigned long>(hr) << std::dec;
      append_line(out.str());
    }
  }
  return hr;
}

void install_managed_texture_use_trace(IDirect3DTexture9 *texture,
                                       std::uint32_t resource_id) {
  const bool surface_hooked = patch_vtable(
      texture, kTextureGetSurfaceLevelIndex, hook_texture_get_surface_level,
      g_real_texture_get_surface_level);
  const bool lock_hooked = patch_vtable(texture, kTextureLockRectIndex,
                                        hook_texture_lock_rect,
                                        g_real_texture_lock_rect);
  const bool unlock_hooked = patch_vtable(texture, kTextureUnlockRectIndex,
                                          hook_texture_unlock_rect,
                                          g_real_texture_unlock_rect);
  std::ostringstream out;
  out << "event=managed_census_use_trace kind=texture2d resource_id="
      << resource_id << " resource=0x" << std::hex
      << reinterpret_cast<std::uintptr_t>(texture) << std::dec
      << " surface_hook=" << surface_hooked << " lock_hook=" << lock_hooked
      << " unlock_hook=" << unlock_hooked;
  append_line(out.str());
}

void install_managed_cube_use_trace(IDirect3DCubeTexture9 *texture,
                                    std::uint32_t resource_id) {
  const bool surface_hooked = patch_vtable(
      texture, kCubeTextureGetCubeMapSurfaceIndex,
      hook_cube_texture_get_cube_map_surface,
      g_real_cube_texture_get_cube_map_surface);
  const bool lock_hooked = patch_vtable(texture, kCubeTextureLockRectIndex,
                                        hook_cube_texture_lock_rect,
                                        g_real_cube_texture_lock_rect);
  const bool unlock_hooked = patch_vtable(texture, kCubeTextureUnlockRectIndex,
                                          hook_cube_texture_unlock_rect,
                                          g_real_cube_texture_unlock_rect);
  std::ostringstream out;
  out << "event=managed_census_use_trace kind=cube_texture resource_id="
      << resource_id << " resource=0x" << std::hex
      << reinterpret_cast<std::uintptr_t>(texture) << std::dec
      << " surface_hook=" << surface_hooked << " lock_hook=" << lock_hooked
      << " unlock_hook=" << unlock_hooked;
  append_line(out.str());
}

void install_managed_index_buffer_use_trace(IDirect3DIndexBuffer9 *buffer,
                                            std::uint32_t resource_id) {
  const bool lock_hooked = patch_vtable(buffer, kIndexBufferLockIndex,
                                        hook_index_buffer_lock,
                                        g_real_index_buffer_lock);
  const bool unlock_hooked = patch_vtable(buffer, kIndexBufferUnlockIndex,
                                          hook_index_buffer_unlock,
                                          g_real_index_buffer_unlock);
  std::ostringstream out;
  out << "event=managed_census_use_trace kind=index_buffer resource_id="
      << resource_id << " resource=0x" << std::hex
      << reinterpret_cast<std::uintptr_t>(buffer) << std::dec
      << " lock_hook=" << lock_hooked << " unlock_hook=" << unlock_hooked;
  append_line(out.str());
}

HRESULT STDMETHODCALLTYPE hook_begin_state_block(IDirect3DDevice9 *device) {
  const HRESULT hr = g_real_begin_state_block(device);
  const bool vertex_buffer_rehooked =
      rehook_vertex_buffer_after_state_block("device_begin_state_block_hook");
  const bool begin_state_block_rehooked =
      patch_vtable(device, kDeviceBeginStateBlockIndex, hook_begin_state_block,
                   g_real_begin_state_block);
  const bool observer_hooks_rehooked =
      rehook_observer_device_hooks_after_state_block(device);

  std::ostringstream out;
  out << "event=begin_state_block_rehook hr=0x" << std::hex
      << static_cast<unsigned long>(hr) << std::dec
      << " vertex_buffer=" << vertex_buffer_rehooked
      << " begin_state_block=" << begin_state_block_rehooked
      << " observer_hooks=" << observer_hooks_rehooked
      << " current_vb_slot=0x" << std::hex
      << current_device_vertex_buffer_slot(device) << std::dec;
  append_line(out.str());
  return hr;
}

[[nodiscard]] bool install_vertex_buffer_use_trace(IDirect3DVertexBuffer9 *buffer) {
  const bool lock_hooked = patch_vtable(buffer, kVertexBufferLockIndex,
                                        hook_vertex_buffer_lock,
                                        g_real_vertex_buffer_lock);
  const bool unlock_hooked = patch_vtable(buffer, kVertexBufferUnlockIndex,
                                          hook_vertex_buffer_unlock,
                                          g_real_vertex_buffer_unlock);
  std::ostringstream out;
  out << "event=managed_vertex_buffer_use_trace buffer=0x" << std::hex
      << reinterpret_cast<std::uintptr_t>(buffer) << std::dec
      << " lock_hook=" << lock_hooked << " unlock_hook=" << unlock_hooked;
  append_line(out.str());
  return lock_hooked && unlock_hooked;
}

template <typename Fn>
[[nodiscard]] bool patch_iat(HMODULE module, const char *function_name, Fn hook,
                             Fn &original) {
  if (!module)
    return false;
  auto *base = reinterpret_cast<std::uint8_t *>(module);
  const auto *dos = reinterpret_cast<const IMAGE_DOS_HEADER *>(base);
  if (dos->e_magic != IMAGE_DOS_SIGNATURE)
    return false;
  const auto *nt =
      reinterpret_cast<const IMAGE_NT_HEADERS32 *>(base + dos->e_lfanew);
  if (nt->Signature != IMAGE_NT_SIGNATURE)
    return false;
  const auto &directory =
      nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
  if (!directory.VirtualAddress)
    return false;

  auto *descriptor = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR *>(
      base + directory.VirtualAddress);
  for (; descriptor->Name; ++descriptor) {
    auto *names = reinterpret_cast<IMAGE_THUNK_DATA32 *>(
        base + (descriptor->OriginalFirstThunk ? descriptor->OriginalFirstThunk
                                              : descriptor->FirstThunk));
    auto *slots = reinterpret_cast<IMAGE_THUNK_DATA32 *>(
        base + descriptor->FirstThunk);
    for (; names->u1.AddressOfData; ++names, ++slots) {
      if (IMAGE_SNAP_BY_ORDINAL32(names->u1.Ordinal))
        continue;
      const auto *import = reinterpret_cast<const IMAGE_IMPORT_BY_NAME *>(
          base + names->u1.AddressOfData);
      if (std::strcmp(reinterpret_cast<const char *>(import->Name),
                      function_name) != 0)
        continue;

      const auto hook_value = static_cast<DWORD>(
          reinterpret_cast<std::uintptr_t>(reinterpret_cast<void *>(hook)));
      if (slots->u1.Function == hook_value)
        return true;
      if (!original)
        original = reinterpret_cast<Fn>(
            static_cast<std::uintptr_t>(slots->u1.Function));
      DWORD old_protect = 0;
      if (!VirtualProtect(&slots->u1.Function, sizeof(slots->u1.Function),
                          PAGE_READWRITE, &old_protect))
        return false;
      slots->u1.Function = hook_value;
      DWORD ignored = 0;
      VirtualProtect(&slots->u1.Function, sizeof(slots->u1.Function),
                     old_protect, &ignored);
      FlushInstructionCache(GetCurrentProcess(), &slots->u1.Function,
                            sizeof(slots->u1.Function));
      return original != nullptr;
    }
  }
  return false;
}

[[nodiscard]] bool matrices_differ(const std::array<D3DMATRIX, 3> &a,
                                   const std::array<D3DMATRIX, 3> &b) {
  return std::memcmp(a.data(), b.data(), sizeof(a)) != 0;
}

void write_sample(const DeviceState &state, const char *boundary) {
  std::ostringstream out;
  out << "event=first_state_sample boundary=" << boundary
      << " device_id=" << state.id
      << " resource_generation=" << state.resource_generation
      << " rt_observed=" << state.render_target_observed;
  if (state.render_target_observed) {
    out << " rt=" << state.render_target.Width << 'x' << state.render_target.Height
        << " rt_format=" << static_cast<unsigned>(state.render_target.Format)
        << " rt_multisample="
        << static_cast<unsigned>(state.render_target.MultiSampleType);
  }
  out << " depth_observed=" << state.depth_observed;
  if (state.depth_observed) {
    out << " depth=" << state.depth.Width << 'x' << state.depth.Height
        << " depth_format=" << static_cast<unsigned>(state.depth.Format)
        << " depth_multisample=" << static_cast<unsigned>(state.depth.MultiSampleType);
  }
  out << " transforms_observed=" << state.transforms_observed
      << " swapchains=" << state.max_swapchains;
  append_line(out.str());
}

void write_complete_sample(const DeviceState &state, const char *boundary) {
  std::ostringstream out;
  out << "event=state_sample boundary=" << boundary
      << " device_id=" << state.id
      << " resource_generation=" << state.resource_generation
      << " rt_observed=" << state.render_target_observed
      << " depth_observed=" << state.depth_observed
      << " transforms_observed=" << state.transforms_observed
      << " swapchains=" << state.max_swapchains;
  append_line(out.str());
}

void write_complete_sample_if_ready(DeviceState &state, const char *boundary) {
  if (state.complete_sample_written || !state.render_target_observed ||
      !state.depth_observed || !state.transforms_observed)
    return;
  state.complete_sample_written = true;
  write_complete_sample(state, boundary);
}

void write_summary(const DeviceState &state) {
  std::ostringstream out;
  out << "event=summary device_id=" << state.id
      << " end_scene=" << state.end_scene_count
      << " present=" << state.present_count << " resets=" << state.reset_count
      << " resource_generation=" << state.resource_generation
      << " max_swapchains=" << state.max_swapchains
      << " rt_observed=" << state.render_target_observed
      << " depth_observed=" << state.depth_observed
      << " transforms_observed=" << state.transforms_observed
      << " transforms_changed=" << state.transforms_changed
      << " OBSERVATION_RESULT OBSERVED";
  append_line(out.str());
}

void sample_device_state(IDirect3DDevice9 *device, DeviceState &state) {
  state.max_swapchains = (std::max)(state.max_swapchains,
                                    device->GetNumberOfSwapChains());

  IDirect3DSurface9 *rt = nullptr;
  if (SUCCEEDED(device->GetRenderTarget(0, &rt)) && rt) {
    D3DSURFACE_DESC desc{};
    if (SUCCEEDED(rt->GetDesc(&desc))) {
      state.render_target = desc;
      state.render_target_observed = true;
    }
    rt->Release();
  }

  IDirect3DSurface9 *depth = nullptr;
  if (SUCCEEDED(device->GetDepthStencilSurface(&depth)) && depth) {
    D3DSURFACE_DESC desc{};
    if (SUCCEEDED(depth->GetDesc(&desc))) {
      state.depth = desc;
      state.depth_observed = true;
    }
    depth->Release();
  }

  std::array<D3DMATRIX, 3> transforms{};
  const bool transforms_ok =
      SUCCEEDED(device->GetTransform(D3DTS_WORLD, &transforms[0])) &&
      SUCCEEDED(device->GetTransform(D3DTS_VIEW, &transforms[1])) &&
      SUCCEEDED(device->GetTransform(D3DTS_PROJECTION, &transforms[2]));
  if (transforms_ok) {
    state.transforms_observed = true;
    if (state.have_last_transforms && matrices_differ(state.last_transforms, transforms))
      state.transforms_changed = true;
    state.last_transforms = transforms;
    state.have_last_transforms = true;
  }
}

void run_shared_relay_test(IDirect3DDevice9 *device) {
  ComPtr<IDirect3DDevice9Ex> device_ex;
  const HRESULT ex_hr = device->QueryInterface(IID_PPV_ARGS(&device_ex));
  if (FAILED(ex_hr) || !device_ex) {
    std::ostringstream out;
    out << "event=real_relay_test device_ex=0 qi_hr=0x" << std::hex
        << static_cast<unsigned long>(ex_hr) << std::dec << " RESULT NO_PATH";
    append_line(out.str());
    return;
  }

  ComPtr<IDirect3DSurface9> source;
  D3DSURFACE_DESC source_desc{};
  HRESULT source_hr = device->GetRenderTarget(0, &source);
  if (SUCCEEDED(source_hr) && source)
    source_hr = source->GetDesc(&source_desc);
  if (FAILED(source_hr) || !source) {
    std::ostringstream out;
    out << "event=real_relay_test device_ex=1 source_hr=0x" << std::hex
        << static_cast<unsigned long>(source_hr) << std::dec << " RESULT FAIL";
    append_line(out.str());
    return;
  }

  HANDLE shared_handle = nullptr;
  ComPtr<IDirect3DTexture9> relay_texture;
  const HRESULT relay_hr = device->CreateTexture(
      source_desc.Width, source_desc.Height, 1, 0, source_desc.Format,
      D3DPOOL_DEFAULT, &relay_texture, &shared_handle);
  ComPtr<IDirect3DSurface9> relay_surface;
  HRESULT relay_surface_hr = relay_hr;
  if (SUCCEEDED(relay_hr) && relay_texture)
    relay_surface_hr = relay_texture->GetSurfaceLevel(0, &relay_surface);

  HRESULT stretch_hr = E_FAIL;
  HRESULT sync_hr = E_FAIL;
  if (SUCCEEDED(relay_surface_hr) && relay_surface) {
    stretch_hr = device->StretchRect(source.Get(), nullptr, relay_surface.Get(),
                                     nullptr, D3DTEXF_NONE);
    if (SUCCEEDED(stretch_hr)) {
      ComPtr<IDirect3DQuery9> query;
      sync_hr = device->CreateQuery(D3DQUERYTYPE_EVENT, &query);
      if (SUCCEEDED(sync_hr) && query) {
        sync_hr = query->Issue(D3DISSUE_END);
        if (SUCCEEDED(sync_hr)) {
          for (std::uint32_t attempt = 0; attempt < 10000; ++attempt) {
            const HRESULT poll = query->GetData(nullptr, 0, D3DGETDATA_FLUSH);
            if (poll == S_OK) {
              sync_hr = S_OK;
              break;
            }
            if (FAILED(poll)) {
              sync_hr = poll;
              break;
            }
            Sleep(0);
          }
        }
      }
    }
  }

  ComPtr<ID3D11Device> d3d11;
  ComPtr<ID3D11DeviceContext> context;
  D3D_FEATURE_LEVEL feature_level{};
  const HRESULT d3d11_hr = D3D11CreateDevice(
      nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT,
      nullptr, 0, D3D11_SDK_VERSION, &d3d11, &feature_level, &context);
  ComPtr<ID3D11Texture2D> opened;
  HRESULT open_hr = d3d11_hr;
  if (SUCCEEDED(d3d11_hr) && d3d11 && shared_handle)
    open_hr = d3d11->OpenSharedResource(shared_handle, IID_PPV_ARGS(&opened));

  std::uint32_t sample_mismatches = 0;
  std::uint32_t samples_compared = 0;
  std::array<std::uint32_t, 5> expected_samples{};
  HRESULT source_readback_hr = E_NOTIMPL;
  HRESULT d3d11_readback_hr = E_NOTIMPL;
  if (SUCCEEDED(open_hr) && opened && source_desc.Format == D3DFMT_A8R8G8B8) {
    ComPtr<IDirect3DSurface9> source_readback;
    source_readback_hr = device->CreateOffscreenPlainSurface(
        source_desc.Width, source_desc.Height, source_desc.Format,
        D3DPOOL_SYSTEMMEM, &source_readback, nullptr);
    if (SUCCEEDED(source_readback_hr))
      source_readback_hr =
          device->GetRenderTargetData(source.Get(), source_readback.Get());

    D3D11_TEXTURE2D_DESC desc{};
    opened->GetDesc(&desc);
    D3D11_TEXTURE2D_DESC staging_desc = desc;
    staging_desc.Usage = D3D11_USAGE_STAGING;
    staging_desc.BindFlags = 0;
    staging_desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    staging_desc.MiscFlags = 0;
    ComPtr<ID3D11Texture2D> staging;
    d3d11_readback_hr = d3d11->CreateTexture2D(&staging_desc, nullptr, &staging);
    D3D11_MAPPED_SUBRESOURCE mapped{};
    if (SUCCEEDED(d3d11_readback_hr)) {
      context->CopyResource(staging.Get(), opened.Get());
      d3d11_readback_hr =
          context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped);
    }

    D3DLOCKED_RECT locked{};
    HRESULT lock_hr = source_readback_hr;
    if (SUCCEEDED(source_readback_hr))
      lock_hr = source_readback->LockRect(&locked, nullptr, D3DLOCK_READONLY);
    if (SUCCEEDED(lock_hr) && SUCCEEDED(d3d11_readback_hr)) {
      const std::array<std::array<UINT, 2>, 5> points{{
          {0, 0},
          {source_desc.Width / 4, source_desc.Height / 4},
          {source_desc.Width / 2, source_desc.Height / 2},
          {(source_desc.Width * 3) / 4, (source_desc.Height * 3) / 4},
          {source_desc.Width - 1, source_desc.Height - 1},
      }};
      for (std::size_t index = 0; index < points.size(); ++index) {
        const auto &point = points[index];
        const auto *d3d9_pixel =
            static_cast<const std::uint8_t *>(locked.pBits) +
            static_cast<std::size_t>(point[1]) * locked.Pitch + point[0] * 4U;
        const auto *d3d11_pixel = static_cast<const std::uint8_t *>(mapped.pData) +
                                  static_cast<std::size_t>(point[1]) *
                                      mapped.RowPitch +
                                  point[0] * 4U;
        std::memcpy(&expected_samples[index], d3d11_pixel,
                    sizeof(expected_samples[index]));
        if (std::memcmp(d3d9_pixel, d3d11_pixel, 4) != 0)
          ++sample_mismatches;
        ++samples_compared;
      }
    }
    if (SUCCEEDED(lock_hr))
      source_readback->UnlockRect();
    if (SUCCEEDED(d3d11_readback_hr))
      context->Unmap(staging.Get(), 0);
    if (FAILED(lock_hr))
      source_readback_hr = lock_hr;
  }

  HRESULT nt_transport_hr = E_NOTIMPL;
  HRESULT nt_copy_sync_hr = E_NOTIMPL;
  HRESULT nt_handle_hr = E_NOTIMPL;
  bool x64_bridge_passed = false;
  if (x86_x64_bridge_test_requested() && SUCCEEDED(open_hr) && opened &&
      samples_compared == expected_samples.size() && sample_mismatches == 0) {
    D3D11_TEXTURE2D_DESC transport_desc{};
    opened->GetDesc(&transport_desc);
    transport_desc.Usage = D3D11_USAGE_DEFAULT;
    transport_desc.CPUAccessFlags = 0;
    transport_desc.BindFlags = D3D11_BIND_SHADER_RESOURCE |
                               D3D11_BIND_RENDER_TARGET;
    transport_desc.MiscFlags = D3D11_RESOURCE_MISC_SHARED_NTHANDLE |
                               D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX;
    ComPtr<ID3D11Texture2D> nt_transport;
    nt_transport_hr =
        d3d11->CreateTexture2D(&transport_desc, nullptr, &nt_transport);
    if (SUCCEEDED(nt_transport_hr) && nt_transport) {
      context->CopyResource(nt_transport.Get(), opened.Get());
      nt_copy_sync_hr = wait_d3d11_event(d3d11.Get(), context.Get());
    }

    ComPtr<IDXGIResource1> dxgi_resource;
    if (SUCCEEDED(nt_copy_sync_hr) && nt_transport)
      nt_handle_hr = nt_transport.As(&dxgi_resource);
    HANDLE nt_handle = nullptr;
    if (SUCCEEDED(nt_handle_hr) && dxgi_resource) {
      SECURITY_ATTRIBUTES security{};
      security.nLength = sizeof(security);
      security.bInheritHandle = TRUE;
      nt_handle_hr = dxgi_resource->CreateSharedHandle(
          &security, DXGI_SHARED_RESOURCE_READ | DXGI_SHARED_RESOURCE_WRITE,
          nullptr, &nt_handle);
    }
    if (SUCCEEDED(nt_handle_hr) && nt_handle) {
      x64_bridge_passed = run_x64_bridge_sink(
          nt_handle, source_desc.Width, source_desc.Height, expected_samples);
      CloseHandle(nt_handle);
    } else {
      std::ostringstream bridge_out;
      bridge_out << "event=real_x86_x64_bridge stage=nt_handle nt_transport_hr=0x"
                 << std::hex << static_cast<unsigned long>(nt_transport_hr)
                 << " nt_copy_sync_hr=0x"
                 << static_cast<unsigned long>(nt_copy_sync_hr)
                 << " nt_handle_hr=0x" << static_cast<unsigned long>(nt_handle_hr)
                 << std::dec << " RESULT FAIL";
      append_line(bridge_out.str());
    }
  }

  const bool passed = SUCCEEDED(relay_hr) && shared_handle &&
                      SUCCEEDED(relay_surface_hr) && SUCCEEDED(stretch_hr) &&
                      SUCCEEDED(sync_hr) && SUCCEEDED(open_hr) &&
                      samples_compared > 0 && sample_mismatches == 0;
  std::ostringstream out;
  out << "event=real_relay_test device_ex=1 rt=" << source_desc.Width << 'x'
      << source_desc.Height << " format="
      << static_cast<unsigned>(source_desc.Format) << " relay_hr=0x" << std::hex
      << static_cast<unsigned long>(relay_hr) << " stretch_hr=0x"
      << static_cast<unsigned long>(stretch_hr) << " sync_hr=0x"
      << static_cast<unsigned long>(sync_hr) << " d3d11_hr=0x"
      << static_cast<unsigned long>(d3d11_hr) << " open_hr=0x"
      << static_cast<unsigned long>(open_hr) << " source_readback_hr=0x"
      << static_cast<unsigned long>(source_readback_hr)
      << " d3d11_readback_hr=0x"
      << static_cast<unsigned long>(d3d11_readback_hr) << std::dec
      << " shared_handle=" << (shared_handle ? 1 : 0)
      << " samples=" << samples_compared
      << " sample_mismatches=" << sample_mismatches;
  if (x86_x64_bridge_test_requested())
    out << " x86_x64_bridge=" << x64_bridge_passed
        << " nt_transport_hr=0x" << std::hex
        << static_cast<unsigned long>(nt_transport_hr)
        << " nt_copy_sync_hr=0x"
        << static_cast<unsigned long>(nt_copy_sync_hr) << " nt_handle_hr=0x"
        << static_cast<unsigned long>(nt_handle_hr) << std::dec;
  out << " RESULT "
      << (passed ? "PASS" : "FAIL");
  append_line(out.str());
}

void observe_scene(IDirect3DDevice9 *device) {
  std::lock_guard lock(g_state_mutex);
  auto it = g_devices.find(device);
  if (it == g_devices.end())
    return;
  DeviceState &state = it->second;
  ++state.end_scene_count;

  const bool sample_now = state.end_scene_count <= 4 ||
                          (state.end_scene_count % 30u) == 0u ||
                          !state.complete_sample_written;
  if (sample_now)
    sample_device_state(device, state);
  write_complete_sample_if_ready(state, "end_scene");

  if (!state.first_sample_written && state.end_scene_count >= 1) {
    state.first_sample_written = true;
    write_sample(state, "end_scene");
  }
  if (!state.summary_written && state.end_scene_count >= kSummaryFrame) {
    state.summary_written = true;
    write_summary(state);
  }
}

ULONG STDMETHODCALLTYPE hook_device_release(IDirect3DDevice9 *device) {
  const ULONG references = g_real_device_release(device);
  if (references == 0) {
    std::shared_ptr<RealBridgeRuntime> multiframe;
    {
      std::lock_guard lock(g_state_mutex);
      auto it = g_devices.find(device);
      if (it != g_devices.end()) {
        if (!it->second.summary_written)
          write_summary(it->second);
        multiframe = std::move(it->second.multiframe);
        g_devices.erase(it);
      }
    }
    if (multiframe)
      multiframe->Shutdown("device_release");
  }
  return references;
}

HRESULT STDMETHODCALLTYPE hook_reset(IDirect3DDevice9 *device,
                                     D3DPRESENT_PARAMETERS *parameters) {
  write_managed_resource_snapshot(device, "before", S_OK);
  probe_managed_texture_content_across_reset(device, "before");
  std::shared_ptr<RealBridgeRuntime> multiframe;
  {
    std::lock_guard lock(g_state_mutex);
    auto it = g_devices.find(device);
    if (it != g_devices.end())
      multiframe = std::move(it->second.multiframe);
  }
  if (multiframe)
    multiframe->Shutdown("reset_begin", true);

  const HRESULT hr = g_real_reset(device, parameters);
  if (SUCCEEDED(hr)) {
    {
      std::lock_guard lock(g_state_mutex);
      auto it = g_devices.find(device);
      if (it != g_devices.end()) {
        ++it->second.reset_count;
        ++it->second.resource_generation;
        it->second.first_sample_written = false;
        it->second.complete_sample_written = false;
        it->second.render_target_observed = false;
        it->second.depth_observed = false;
        it->second.transforms_observed = false;
        it->second.transforms_changed = false;
        it->second.have_last_transforms = false;
        it->second.max_swapchains = 0;
        std::ostringstream out;
        out << "event=reset device_id=" << it->second.id
            << " resource_generation=" << it->second.resource_generation
            << " width=" << (parameters ? parameters->BackBufferWidth : 0)
            << " height=" << (parameters ? parameters->BackBufferHeight : 0);
        append_line(out.str());
      }
    }
  }
  write_managed_resource_snapshot(device, SUCCEEDED(hr) ? "after" : "failed", hr);
  if (SUCCEEDED(hr))
    probe_managed_texture_content_across_reset(device, "after");
  return hr;
}

HRESULT STDMETHODCALLTYPE hook_create_texture(
    IDirect3DDevice9 *device, UINT width, UINT height, UINT levels, DWORD usage,
    D3DFORMAT format, D3DPOOL pool, IDirect3DTexture9 **texture,
    HANDLE *shared_handle) {
  const HRESULT original_hr = g_real_create_texture(
      device, width, height, levels, usage, format, pool, texture, shared_handle);
  HRESULT hr = original_hr;
  bool fallback_attempted = false;
  const bool fallback_requested = managed_texture_fallback_requested();
  const bool semantic_adaptation_requested =
      managed_semantic_adaptation_requested();
  const auto semantic_plan =
      ltr::d3d9_real_observer::plan_managed_resource_adaptation(
          ltr::d3d9_real_observer::ManagedResourceClass::texture2d, usage, pool,
          shared_handle != nullptr, original_hr);
  bool semantic_adaptation_attempted = false;
  const bool levels_match = levels == 0;
  const bool usage_matches = usage == 0;
  const bool pool_matches = pool == D3DPOOL_MANAGED;
  const bool shared_handle_matches = !shared_handle;
  const bool hr_matches = original_hr == D3DERR_INVALIDCALL;
  const bool startup_a8r8g8b8_matches =
      width == 16 && height == 16 && format == D3DFMT_A8R8G8B8;
  const bool startup_dxt1_matches =
      width == 64 && height == 64 && format == D3DFMT_DXT1;
  const bool observed_managed_texture =
      (startup_a8r8g8b8_matches || startup_dxt1_matches) && levels_match &&
      usage_matches && pool_matches && shared_handle_matches;
  if (semantic_adaptation_requested && semantic_plan.retry) {
    semantic_adaptation_attempted = true;
    if (texture)
      *texture = nullptr;
    hr = g_real_create_texture(device, width, height, levels,
                               semantic_plan.usage, format, semantic_plan.pool,
                               texture, nullptr);
  } else if (fallback_requested && observed_managed_texture && hr_matches) {
    fallback_attempted = true;
    if (texture)
      *texture = nullptr;
    hr = g_real_create_texture(device, width, height, levels, usage, format,
                               D3DPOOL_DEFAULT, texture, nullptr);
  }
  std::uint32_t census_resource_id = 0;
  if (managed_resource_trace_requested() && pool == D3DPOOL_MANAGED &&
      SUCCEEDED(hr) &&
      texture && *texture) {
    census_resource_id = track_managed_resource(
        device, *texture, ManagedResourceKind::texture2d);
    install_managed_texture_use_trace(*texture, census_resource_id);
  }
  if (resource_census_requested() ||
      ((trace_exceptions_requested() || fallback_attempted ||
        semantic_adaptation_requested) &&
       (pool == D3DPOOL_MANAGED || FAILED(hr)))) {
    const LONG ordinal = InterlockedIncrement(&g_create_texture_trace_count);
    if (resource_census_requested() || ordinal <= 64) {
      std::ostringstream out;
      out << "event=create_texture_trace ordinal=" << ordinal
          << " width=" << width << " height=" << height
          << " levels=" << levels << " usage=0x" << std::hex << usage
          << std::dec << " format=" << static_cast<unsigned long>(format)
          << " format_hex=0x" << std::hex
          << static_cast<unsigned long>(format) << std::dec
          << " pool=" << static_cast<unsigned long>(pool)
          << " original_hr=0x" << std::hex
          << static_cast<unsigned long>(original_hr)
          << " final_hr=0x" << static_cast<unsigned long>(hr) << std::dec
          << " fallback_requested=" << fallback_requested
          << " fallback_default=" << fallback_attempted
          << " semantic_adaptation_requested="
          << semantic_adaptation_requested
          << " semantic_adaptation=" << semantic_adaptation_attempted
          << " retry_usage=0x" << std::hex << semantic_plan.usage << std::dec
          << " retry_pool=" << static_cast<unsigned long>(semantic_plan.pool)
          << " match_levels=" << levels_match
          << " match_usage=" << usage_matches
          << " match_startup_a8r8g8b8=" << startup_a8r8g8b8_matches
          << " match_startup_dxt1=" << startup_dxt1_matches
          << " match_pool=" << pool_matches
          << " match_shared_handle=" << shared_handle_matches
           << " match_hr=" << hr_matches
           << " census_resource_id=" << census_resource_id
           << " texture="
          << ((texture && *texture) ? "non_null" : "null")
          << " shared_handle_arg=" << (shared_handle ? 1 : 0)
          << " current_vb_slot=0x" << std::hex
          << current_device_vertex_buffer_slot(device) << std::dec;
      append_line(out.str());
    }
  }
  return hr;
}

HRESULT STDMETHODCALLTYPE hook_create_volume_texture(
    IDirect3DDevice9 *device, UINT width, UINT height, UINT depth, UINT levels,
    DWORD usage, D3DFORMAT format, D3DPOOL pool,
    IDirect3DVolumeTexture9 **texture, HANDLE *shared_handle) {
  const HRESULT hr = g_real_create_volume_texture(
      device, width, height, depth, levels, usage, format, pool, texture,
      shared_handle);
  if (resource_census_requested() ||
      ((trace_exceptions_requested() || managed_texture_fallback_requested() ||
        managed_semantic_adaptation_requested()) &&
       (pool == D3DPOOL_MANAGED || FAILED(hr)))) {
    const LONG ordinal =
        InterlockedIncrement(&g_create_volume_texture_trace_count);
    if (resource_census_requested() || ordinal <= 64) {
      std::ostringstream out;
      out << "event=create_volume_texture_trace ordinal=" << ordinal
          << " width=" << width << " height=" << height
          << " depth=" << depth << " levels=" << levels
          << " usage=0x" << std::hex << usage << std::dec
          << " format=" << static_cast<unsigned long>(format)
          << " format_hex=0x" << std::hex
          << static_cast<unsigned long>(format) << std::dec
          << " pool=" << static_cast<unsigned long>(pool)
          << " hr=0x" << std::hex << static_cast<unsigned long>(hr) << std::dec
          << " texture="
          << ((texture && *texture) ? "non_null" : "null")
          << " shared_handle_arg=" << (shared_handle ? 1 : 0);
      append_line(out.str());
    }
  }
  return hr;
}

HRESULT STDMETHODCALLTYPE hook_create_cube_texture(
    IDirect3DDevice9 *device, UINT edge_length, UINT levels, DWORD usage,
    D3DFORMAT format, D3DPOOL pool, IDirect3DCubeTexture9 **texture,
    HANDLE *shared_handle) {
  const HRESULT original_hr = g_real_create_cube_texture(
      device, edge_length, levels, usage, format, pool, texture, shared_handle);
  HRESULT hr = original_hr;
  bool fallback_attempted = false;
  const bool fallback_requested = managed_texture_fallback_requested();
  const bool semantic_adaptation_requested =
      managed_semantic_adaptation_requested();
  const auto semantic_plan =
      ltr::d3d9_real_observer::plan_managed_resource_adaptation(
          ltr::d3d9_real_observer::ManagedResourceClass::cube_texture, usage,
          pool, shared_handle != nullptr, original_hr);
  bool semantic_adaptation_attempted = false;
  const bool edge_matches = edge_length == 128;
  const bool levels_match = levels == 1;
  const bool usage_matches = usage == 0;
  const bool format_matches = format == D3DFMT_A8R8G8B8;
  const bool pool_matches = pool == D3DPOOL_MANAGED;
  const bool shared_handle_matches = !shared_handle;
  const bool hr_matches = original_hr == D3DERR_INVALIDCALL;
  const bool observed_managed_cube =
      edge_matches && levels_match && usage_matches && format_matches &&
      pool_matches && shared_handle_matches;
  if (semantic_adaptation_requested && semantic_plan.retry) {
    semantic_adaptation_attempted = true;
    if (texture)
      *texture = nullptr;
    hr = g_real_create_cube_texture(device, edge_length, levels,
                                    semantic_plan.usage, format,
                                    semantic_plan.pool, texture, nullptr);
  } else if (fallback_requested && observed_managed_cube && hr_matches) {
    fallback_attempted = true;
    if (texture)
      *texture = nullptr;
    hr = g_real_create_cube_texture(device, edge_length, levels, usage, format,
                                    D3DPOOL_DEFAULT, texture, nullptr);
  }
  std::uint32_t census_resource_id = 0;
  if (managed_resource_trace_requested() && pool == D3DPOOL_MANAGED &&
      SUCCEEDED(hr) &&
      texture && *texture) {
    census_resource_id = track_managed_resource(
        device, *texture, ManagedResourceKind::cube_texture);
    install_managed_cube_use_trace(*texture, census_resource_id);
  }
  if (resource_census_requested() ||
      ((trace_exceptions_requested() || managed_texture_fallback_requested() ||
        semantic_adaptation_requested) &&
       (pool == D3DPOOL_MANAGED || FAILED(hr)))) {
    const LONG ordinal = InterlockedIncrement(&g_create_cube_texture_trace_count);
    if (resource_census_requested() || ordinal <= 64) {
      std::ostringstream out;
      out << "event=create_cube_texture_trace ordinal=" << ordinal
          << " edge=" << edge_length << " levels=" << levels
          << " usage=0x" << std::hex << usage
          << std::dec << " format=" << static_cast<unsigned long>(format)
          << " format_hex=0x" << std::hex
          << static_cast<unsigned long>(format) << std::dec
          << " pool=" << static_cast<unsigned long>(pool)
          << " original_hr=0x" << std::hex
          << static_cast<unsigned long>(original_hr)
          << " final_hr=0x" << static_cast<unsigned long>(hr) << std::dec
          << " fallback_requested=" << fallback_requested
          << " fallback_default=" << fallback_attempted
          << " semantic_adaptation_requested="
          << semantic_adaptation_requested
          << " semantic_adaptation=" << semantic_adaptation_attempted
          << " retry_usage=0x" << std::hex << semantic_plan.usage << std::dec
          << " retry_pool=" << static_cast<unsigned long>(semantic_plan.pool)
          << " match_edge=" << edge_matches
          << " match_levels=" << levels_match
          << " match_usage=" << usage_matches
          << " match_format=" << format_matches
          << " match_pool=" << pool_matches
          << " match_shared_handle=" << shared_handle_matches
           << " match_hr=" << hr_matches
           << " census_resource_id=" << census_resource_id
           << " texture="
          << ((texture && *texture) ? "non_null" : "null")
          << " shared_handle_arg=" << (shared_handle ? 1 : 0)
          << " current_vb_slot=0x" << std::hex
          << current_device_vertex_buffer_slot(device) << std::dec;
      append_line(out.str());
    }
  }
  return hr;
}

HRESULT STDMETHODCALLTYPE hook_create_vertex_buffer(
    IDirect3DDevice9 *device, UINT length, DWORD usage, DWORD fvf, D3DPOOL pool,
    IDirect3DVertexBuffer9 **buffer, HANDLE *shared_handle) {
  HRESULT hr = g_real_create_vertex_buffer(device, length, usage, fvf, pool,
                                           buffer, shared_handle);
  const HRESULT original_hr = hr;
  const bool fallback_requested = managed_vertex_buffer_fallback_requested();
  const bool semantic_adaptation_requested =
      managed_semantic_adaptation_requested();
  const auto semantic_plan =
      ltr::d3d9_real_observer::plan_managed_resource_adaptation(
          ltr::d3d9_real_observer::ManagedResourceClass::vertex_buffer, usage,
          pool, shared_handle != nullptr, original_hr);
  const bool length_matches = length == 0x40000;
  const bool usage_matches = usage == 0x8;
  const bool fvf_matches = fvf == 0;
  const bool pool_matches = pool == D3DPOOL_MANAGED;
  const bool shared_handle_matches = shared_handle == nullptr;
  const bool hr_matches = original_hr == D3DERR_INVALIDCALL;
  const bool observed_managed_vertex_buffer =
      length_matches && usage_matches && fvf_matches && pool_matches &&
      shared_handle_matches;
  bool fallback_attempted = false;
  bool semantic_adaptation_attempted = false;
  if (semantic_adaptation_requested && semantic_plan.retry) {
    semantic_adaptation_attempted = true;
    if (buffer)
      *buffer = nullptr;
    hr = g_real_create_vertex_buffer(device, length, semantic_plan.usage, fvf,
                                     semantic_plan.pool, buffer, nullptr);
  } else if (fallback_requested && observed_managed_vertex_buffer && hr_matches) {
    fallback_attempted = true;
    if (buffer)
      *buffer = nullptr;
    hr = g_real_create_vertex_buffer(device, length, usage, fvf, D3DPOOL_DEFAULT,
                                     buffer, nullptr);
  }
  std::uint32_t census_resource_id = 0;
  if (managed_resource_trace_requested() && pool == D3DPOOL_MANAGED &&
      SUCCEEDED(hr) &&
      buffer && *buffer) {
    census_resource_id = track_managed_resource(
        device, *buffer, ManagedResourceKind::vertex_buffer);
    const bool census_hooks = install_vertex_buffer_use_trace(*buffer);
    std::ostringstream census_out;
    census_out << "event=managed_census_use_trace kind=vertex_buffer resource_id="
               << census_resource_id << " resource=0x" << std::hex
               << reinterpret_cast<std::uintptr_t>(*buffer) << std::dec
               << " hooks=" << census_hooks;
    append_line(census_out.str());
  }
  if (resource_census_requested() ||
      ((trace_exceptions_requested() || managed_texture_fallback_requested() ||
        managed_vertex_buffer_fallback_requested() ||
        semantic_adaptation_requested) &&
       (pool == D3DPOOL_MANAGED || FAILED(hr)))) {
    const LONG ordinal = InterlockedIncrement(&g_create_vertex_buffer_trace_count);
    if (resource_census_requested() || ordinal <= 64) {
      std::ostringstream out;
      out << "event=create_vertex_buffer_trace ordinal=" << ordinal
          << " length=" << length << " usage=0x" << std::hex << usage
          << " fvf=0x" << fvf << std::dec
          << " pool=" << static_cast<unsigned long>(pool)
          << " original_hr=0x" << std::hex
          << static_cast<unsigned long>(original_hr)
          << " final_hr=0x" << static_cast<unsigned long>(hr) << std::dec
          << " fallback_requested=" << fallback_requested
          << " fallback_default=" << fallback_attempted
          << " semantic_adaptation_requested="
          << semantic_adaptation_requested
          << " semantic_adaptation=" << semantic_adaptation_attempted
          << " retry_usage=0x" << std::hex << semantic_plan.usage << std::dec
          << " retry_pool=" << static_cast<unsigned long>(semantic_plan.pool)
          << " match_length=" << length_matches
          << " match_usage=" << usage_matches << " match_fvf=" << fvf_matches
          << " match_pool=" << pool_matches
          << " match_shared_handle=" << shared_handle_matches
           << " match_hr=" << hr_matches
           << " census_resource_id=" << census_resource_id
           << " buffer=" << ((buffer && *buffer) ? "non_null" : "null")
          << " shared_handle_arg=" << (shared_handle ? 1 : 0);
      append_line(out.str());
    }
  }
  return hr;
}

HRESULT STDMETHODCALLTYPE hook_create_index_buffer(
    IDirect3DDevice9 *device, UINT length, DWORD usage, D3DFORMAT format,
    D3DPOOL pool, IDirect3DIndexBuffer9 **buffer, HANDLE *shared_handle) {
  HRESULT hr = g_real_create_index_buffer(device, length, usage, format, pool,
                                          buffer, shared_handle);
  const HRESULT original_hr = hr;
  const bool fallback_requested = managed_index_buffer_fallback_requested();
  const bool semantic_adaptation_requested =
      managed_semantic_adaptation_requested();
  const auto semantic_plan =
      ltr::d3d9_real_observer::plan_managed_resource_adaptation(
          ltr::d3d9_real_observer::ManagedResourceClass::index_buffer, usage,
          pool, shared_handle != nullptr, original_hr);
  const bool length_matches = length == 0x20000;
  const bool usage_matches = usage == 0x8;
  const bool format_matches = format == D3DFMT_INDEX16;
  const bool pool_matches = pool == D3DPOOL_MANAGED;
  const bool shared_handle_matches = shared_handle == nullptr;
  const bool hr_matches = original_hr == D3DERR_INVALIDCALL;
  const bool observed_managed_index_buffer =
      length_matches && usage_matches && format_matches && pool_matches &&
      shared_handle_matches;
  bool fallback_attempted = false;
  bool semantic_adaptation_attempted = false;
  if (semantic_adaptation_requested && semantic_plan.retry) {
    semantic_adaptation_attempted = true;
    if (buffer)
      *buffer = nullptr;
    hr = g_real_create_index_buffer(device, length, semantic_plan.usage, format,
                                    semantic_plan.pool, buffer, nullptr);
  } else if (fallback_requested && observed_managed_index_buffer && hr_matches) {
    fallback_attempted = true;
    if (buffer)
      *buffer = nullptr;
    hr = g_real_create_index_buffer(device, length, usage, format,
                                    D3DPOOL_DEFAULT, buffer, nullptr);
  }
  std::uint32_t census_resource_id = 0;
  if (managed_resource_trace_requested() && pool == D3DPOOL_MANAGED &&
      SUCCEEDED(hr) &&
      buffer && *buffer) {
    census_resource_id = track_managed_resource(
        device, *buffer, ManagedResourceKind::index_buffer);
    install_managed_index_buffer_use_trace(*buffer, census_resource_id);
  }
  if (resource_census_requested() ||
      ((trace_exceptions_requested() || managed_texture_fallback_requested() ||
        managed_index_buffer_fallback_requested() ||
        semantic_adaptation_requested) &&
       (pool == D3DPOOL_MANAGED || FAILED(hr)))) {
    const LONG ordinal = InterlockedIncrement(&g_create_index_buffer_trace_count);
    if (resource_census_requested() || ordinal <= 64) {
      std::ostringstream out;
      out << "event=create_index_buffer_trace ordinal=" << ordinal
          << " length=" << length << " usage=0x" << std::hex << usage
          << std::dec << " format=" << static_cast<unsigned long>(format)
          << " format_hex=0x" << std::hex
          << static_cast<unsigned long>(format) << std::dec
          << " pool=" << static_cast<unsigned long>(pool)
          << " original_hr=0x" << std::hex
          << static_cast<unsigned long>(original_hr)
          << " final_hr=0x" << static_cast<unsigned long>(hr) << std::dec
          << " fallback_requested=" << fallback_requested
          << " fallback_default=" << fallback_attempted
          << " semantic_adaptation_requested="
          << semantic_adaptation_requested
          << " semantic_adaptation=" << semantic_adaptation_attempted
          << " retry_usage=0x" << std::hex << semantic_plan.usage << std::dec
          << " retry_pool=" << static_cast<unsigned long>(semantic_plan.pool)
          << " match_length=" << length_matches
          << " match_usage=" << usage_matches
          << " match_format=" << format_matches
          << " match_pool=" << pool_matches
          << " match_shared_handle=" << shared_handle_matches
           << " match_hr=" << hr_matches
           << " census_resource_id=" << census_resource_id
           << " buffer=" << ((buffer && *buffer) ? "non_null" : "null")
          << " shared_handle_arg=" << (shared_handle ? 1 : 0);
      append_line(out.str());
    }
  }
  return hr;
}

HRESULT STDMETHODCALLTYPE hook_set_texture(IDirect3DDevice9 *device, DWORD stage,
                                            IDirect3DBaseTexture9 *texture) {
  ManagedResourceTrace trace{};
  std::uint32_t current_generation = 0;
  const bool stale_binding = take_stale_managed_binding_observation(
      device, texture, kManagedBindingTexture,
      ManagedResourceKind::texture2d, true, trace, current_generation);
  const HRESULT hr = g_real_set_texture(device, stage, texture);
  if (stale_binding)
    write_stale_managed_binding(trace, current_generation, "set_texture",
                                static_cast<UINT>(stage), hr);
  return hr;
}

HRESULT STDMETHODCALLTYPE hook_set_stream_source(
    IDirect3DDevice9 *device, UINT stream_number,
    IDirect3DVertexBuffer9 *stream_data, UINT offset_in_bytes, UINT stride) {
  ManagedResourceTrace trace{};
  std::uint32_t current_generation = 0;
  const bool stale_binding = take_stale_managed_binding_observation(
      device, stream_data, kManagedBindingStreamSource,
      ManagedResourceKind::vertex_buffer, false, trace, current_generation);
  const HRESULT hr = g_real_set_stream_source(device, stream_number, stream_data,
                                               offset_in_bytes, stride);
  if (stale_binding)
    write_stale_managed_binding(trace, current_generation, "set_stream_source",
                                stream_number, hr);
  return hr;
}

HRESULT STDMETHODCALLTYPE hook_set_indices(IDirect3DDevice9 *device,
                                            IDirect3DIndexBuffer9 *index_data) {
  ManagedResourceTrace trace{};
  std::uint32_t current_generation = 0;
  const bool stale_binding = take_stale_managed_binding_observation(
      device, index_data, kManagedBindingIndices,
      ManagedResourceKind::index_buffer, false, trace, current_generation);
  const HRESULT hr = g_real_set_indices(device, index_data);
  if (stale_binding)
    write_stale_managed_binding(trace, current_generation, "set_indices", 0,
                                hr);
  return hr;
}

HRESULT STDMETHODCALLTYPE hook_draw_primitive(IDirect3DDevice9 *device,
                                               D3DPRIMITIVETYPE primitive_type,
                                               UINT start_vertex,
                                               UINT primitive_count) {
  observe_managed_draw_state(device, "draw_primitive");
  return g_real_draw_primitive(device, primitive_type, start_vertex,
                               primitive_count);
}

HRESULT STDMETHODCALLTYPE hook_draw_indexed_primitive(
    IDirect3DDevice9 *device, D3DPRIMITIVETYPE primitive_type,
    INT base_vertex_index, UINT min_vertex_index, UINT num_vertices,
    UINT start_index, UINT primitive_count) {
  observe_managed_draw_state(device, "draw_indexed_primitive");
  return g_real_draw_indexed_primitive(
      device, primitive_type, base_vertex_index, min_vertex_index, num_vertices,
      start_index, primitive_count);
}

HRESULT STDMETHODCALLTYPE hook_create_vertex_declaration(
    IDirect3DDevice9 *device, const D3DVERTEXELEMENT9 *elements,
    IDirect3DVertexDeclaration9 **declaration) {
  const HRESULT hr =
      g_real_create_vertex_declaration(device, elements, declaration);
  if (resource_census_requested() || trace_exceptions_requested() ||
      managed_texture_fallback_requested() ||
      managed_index_buffer_fallback_requested() ||
      managed_semantic_adaptation_requested()) {
    const LONG ordinal =
        InterlockedIncrement(&g_create_vertex_declaration_trace_count);
    if (ordinal <= 64) {
      std::ostringstream out;
      out << "event=create_vertex_declaration_trace ordinal=" << ordinal
          << " hr=0x" << std::hex << static_cast<unsigned long>(hr) << std::dec
          << " declaration="
          << ((declaration && *declaration) ? "non_null" : "null")
          << " current_vb_slot=0x" << std::hex
          << current_device_vertex_buffer_slot(device) << std::dec;
      bool terminated = false;
      if (elements) {
        for (std::size_t index = 0; index < 32; ++index) {
          const D3DVERTEXELEMENT9 &element = elements[index];
          out << " e" << index << "=" << element.Stream << ":"
              << element.Offset << ":" << static_cast<unsigned>(element.Type)
              << ":" << static_cast<unsigned>(element.Method) << ":"
              << static_cast<unsigned>(element.Usage) << ":"
              << static_cast<unsigned>(element.UsageIndex);
          if (element.Stream == 0xFF && element.Type == D3DDECLTYPE_UNUSED) {
            terminated = true;
            break;
          }
        }
      }
      out << " terminated=" << terminated;
      append_line(out.str());
    }
  }
  return hr;
}

HRESULT STDMETHODCALLTYPE hook_present(IDirect3DDevice9 *device,
                                       const RECT *source, const RECT *dest,
                                       HWND window, const RGNDATA *dirty) {
  bool run_relay_test = false;
  if (shared_relay_test_requested()) {
    std::lock_guard lock(g_state_mutex);
    auto it = g_devices.find(device);
    if (it != g_devices.end() && !it->second.relay_test_attempted) {
      it->second.relay_test_attempted = true;
      run_relay_test = true;
    }
  }
  if (run_relay_test)
    run_shared_relay_test(device);

  std::shared_ptr<RealBridgeRuntime> multiframe;
  if (multiframe_bridge_test_requested()) {
    std::lock_guard lock(g_state_mutex);
    auto it = g_devices.find(device);
    if (it != g_devices.end()) {
      if (!it->second.multiframe) {
        it->second.multiframe = std::make_shared<RealBridgeRuntime>(
            it->second.id, it->second.resource_generation);
      }
      multiframe = it->second.multiframe;
    }
  }
  if (multiframe)
    multiframe->Capture(device);

  const LONG present_slot_ordinal =
      (trace_exceptions_requested() || managed_texture_fallback_requested())
          ? InterlockedIncrement(&g_present_vb_slot_trace_count)
          : 0;
  if (present_slot_ordinal > 0 && present_slot_ordinal <= 4) {
    std::ostringstream out;
    out << "event=device_vb_slot_trace boundary=present_before ordinal="
        << present_slot_ordinal << " device=0x" << std::hex
        << reinterpret_cast<std::uintptr_t>(device) << " vb_slot=0x"
        << current_device_vertex_buffer_slot(device) << " expected_hook=0x"
        << reinterpret_cast<std::uintptr_t>(&hook_create_vertex_buffer)
        << " original=0x"
        << reinterpret_cast<std::uintptr_t>(g_real_create_vertex_buffer)
        << std::dec;
    append_line(out.str());
  }
  const HRESULT hr = g_real_present(device, source, dest, window, dirty);
  if (present_slot_ordinal > 0 && present_slot_ordinal <= 4) {
    std::ostringstream out;
    out << "event=device_vb_slot_trace boundary=present_after ordinal="
        << present_slot_ordinal << " device=0x" << std::hex
        << reinterpret_cast<std::uintptr_t>(device) << " vb_slot=0x"
        << current_device_vertex_buffer_slot(device) << std::dec;
    append_line(out.str());
  }
  bool run_video_apply = false;
  UINT video_apply_width = 0;
  UINT video_apply_height = 0;
  if (SUCCEEDED(hr)) {
    if (InterlockedCompareExchange(&g_managed_vertex_buffer_fallback_ready, 1,
                                   1) == 1) {
      const LONG ordinal =
          InterlockedIncrement(&g_post_fallback_present_count);
      if (ordinal <= 4 || (ordinal % 30) == 0) {
        std::ostringstream out;
        out << "event=post_managed_vertex_buffer_fallback_progress boundary=present"
            << " ordinal=" << ordinal;
        append_line(out.str());
      }
    }
    std::lock_guard lock(g_state_mutex);
    auto it = g_devices.find(device);
    if (it != g_devices.end()) {
      DeviceState &state = it->second;
      ++state.present_count;
      if ((state.end_scene_count == 0 &&
           (state.present_count <= 4 || (state.present_count % 30u) == 0u)) ||
          !state.complete_sample_written)
        sample_device_state(device, state);
      write_complete_sample_if_ready(state, "present");
      if (!state.first_sample_written) {
        sample_device_state(device, state);
        state.first_sample_written = true;
        write_sample(state, "present");
      }
      if (!state.summary_written &&
          (state.present_count >= kSummaryFrame ||
           state.end_scene_count >= kSummaryFrame)) {
        state.summary_written = true;
        write_summary(state);
      }
      if (video_apply_probe_requested() &&
          !state.video_apply_probe_attempted &&
          state.present_count >= 60 && state.render_target_observed) {
        state.video_apply_probe_attempted = true;
        run_video_apply = true;
        video_apply_width = state.render_target.Width;
        video_apply_height = state.render_target.Height;
      }
    }
  }
  if (run_video_apply)
    run_video_apply_probe(video_apply_width, video_apply_height);
  return hr;
}

HRESULT STDMETHODCALLTYPE hook_end_scene(IDirect3DDevice9 *device) {
  const HRESULT hr = g_real_end_scene(device);
  if (SUCCEEDED(hr)) {
    if (InterlockedCompareExchange(&g_managed_vertex_buffer_fallback_ready, 1,
                                   1) == 1) {
      const LONG ordinal =
          InterlockedIncrement(&g_post_fallback_end_scene_count);
      if (ordinal <= 4 || (ordinal % 30) == 0) {
        std::ostringstream out;
        out << "event=post_managed_vertex_buffer_fallback_progress boundary=end_scene"
            << " ordinal=" << ordinal;
        append_line(out.str());
      }
    }
    observe_scene(device);
  }
  return hr;
}

[[nodiscard]] bool
rehook_observer_device_hooks_after_state_block(IDirect3DDevice9 *device) {
  bool hooked =
      patch_vtable(device, kDeviceReleaseIndex, hook_device_release,
                   g_real_device_release) &&
      patch_vtable(device, kDeviceResetIndex, hook_reset, g_real_reset) &&
      patch_vtable(device, kDevicePresentIndex, hook_present, g_real_present) &&
      patch_vtable(device, kDeviceEndSceneIndex, hook_end_scene, g_real_end_scene);

  if (resource_census_requested() || trace_exceptions_requested() ||
      managed_texture_fallback_requested() ||
      managed_index_buffer_fallback_requested() ||
      managed_semantic_adaptation_requested()) {
    hooked =
        patch_vtable(device, kDeviceCreateTextureIndex, hook_create_texture,
                     g_real_create_texture) &&
        patch_vtable(device, kDeviceCreateVolumeTextureIndex,
                     hook_create_volume_texture, g_real_create_volume_texture) &&
        patch_vtable(device, kDeviceCreateCubeTextureIndex,
                     hook_create_cube_texture, g_real_create_cube_texture) &&
        patch_vtable(device, kDeviceCreateIndexBufferIndex,
                     hook_create_index_buffer, g_real_create_index_buffer) &&
        patch_vtable(device, kDeviceSetTextureIndex, hook_set_texture,
                     g_real_set_texture) &&
        patch_vtable(device, kDeviceSetStreamSourceIndex,
                     hook_set_stream_source, g_real_set_stream_source) &&
        patch_vtable(device, kDeviceSetIndicesIndex, hook_set_indices,
                     g_real_set_indices) &&
        patch_vtable(device, kDeviceDrawPrimitiveIndex, hook_draw_primitive,
                     g_real_draw_primitive) &&
        patch_vtable(device, kDeviceDrawIndexedPrimitiveIndex,
                     hook_draw_indexed_primitive,
                     g_real_draw_indexed_primitive) &&
        patch_vtable(device, kDeviceCreateVertexDeclarationIndex,
                     hook_create_vertex_declaration,
                     g_real_create_vertex_declaration) &&
        hooked;
  }

  return hooked;
}

HRESULT STDMETHODCALLTYPE hook_create_device(
    IDirect3D9 *d3d9, UINT adapter, D3DDEVTYPE type, HWND focus, DWORD behavior,
    D3DPRESENT_PARAMETERS *parameters, IDirect3DDevice9 **device) {
  const HRESULT hr = g_real_create_device(d3d9, adapter, type, focus, behavior,
                                          parameters, device);
  if (FAILED(hr) || !device || !*device)
    return hr;

  DeviceState state{};
  {
    std::lock_guard lock(g_state_mutex);
    state.id = g_next_device_id++;
    g_devices.emplace(*device, state);
  }

  const bool hooked =
      patch_vtable(*device, kDeviceReleaseIndex, hook_device_release,
                   g_real_device_release) &&
      patch_vtable(*device, kDeviceResetIndex, hook_reset, g_real_reset) &&
      patch_vtable(*device, kDevicePresentIndex, hook_present, g_real_present) &&
      patch_vtable(*device, kDeviceEndSceneIndex, hook_end_scene, g_real_end_scene);

  bool texture_trace_hooked = false;
  bool volume_texture_trace_hooked = false;
  bool cube_texture_trace_hooked = false;
  bool vertex_buffer_trace_hooked = false;
  bool index_buffer_trace_hooked = false;
  bool vertex_declaration_trace_hooked = false;
  bool set_texture_trace_hooked = false;
  bool set_stream_source_trace_hooked = false;
  bool set_indices_trace_hooked = false;
  bool draw_primitive_trace_hooked = false;
  bool draw_indexed_primitive_trace_hooked = false;
  bool canonical_vertex_buffer_hooked = false;
  bool begin_state_block_rehook_hooked = false;
  if (resource_census_requested() || trace_exceptions_requested() ||
      managed_texture_fallback_requested() ||
      managed_index_buffer_fallback_requested() ||
      managed_semantic_adaptation_requested()) {
    texture_trace_hooked = patch_vtable(
        *device, kDeviceCreateTextureIndex, hook_create_texture,
        g_real_create_texture);
    volume_texture_trace_hooked = patch_vtable(
        *device, kDeviceCreateVolumeTextureIndex, hook_create_volume_texture,
        g_real_create_volume_texture);
    cube_texture_trace_hooked = patch_vtable(
        *device, kDeviceCreateCubeTextureIndex, hook_create_cube_texture,
        g_real_create_cube_texture);
    vertex_buffer_trace_hooked = patch_vtable(
        *device, kDeviceCreateVertexBufferIndex, hook_create_vertex_buffer,
        g_real_create_vertex_buffer);
    index_buffer_trace_hooked = patch_vtable(
        *device, kDeviceCreateIndexBufferIndex, hook_create_index_buffer,
        g_real_create_index_buffer);
    set_texture_trace_hooked = patch_vtable(
        *device, kDeviceSetTextureIndex, hook_set_texture, g_real_set_texture);
    set_stream_source_trace_hooked = patch_vtable(
        *device, kDeviceSetStreamSourceIndex, hook_set_stream_source,
        g_real_set_stream_source);
    set_indices_trace_hooked = patch_vtable(
        *device, kDeviceSetIndicesIndex, hook_set_indices, g_real_set_indices);
    draw_primitive_trace_hooked = patch_vtable(
        *device, kDeviceDrawPrimitiveIndex, hook_draw_primitive,
        g_real_draw_primitive);
    draw_indexed_primitive_trace_hooked = patch_vtable(
        *device, kDeviceDrawIndexedPrimitiveIndex, hook_draw_indexed_primitive,
        g_real_draw_indexed_primitive);
    vertex_declaration_trace_hooked = patch_vtable(
        *device, kDeviceCreateVertexDeclarationIndex,
        hook_create_vertex_declaration, g_real_create_vertex_declaration);
    if (begin_state_block_vertex_buffer_rehook_requested())
      begin_state_block_rehook_hooked = patch_vtable(
          *device, kDeviceBeginStateBlockIndex, hook_begin_state_block,
          g_real_begin_state_block);
    if (canonical_vertex_buffer_hook_requested())
      canonical_vertex_buffer_hooked = install_canonical_vertex_buffer_hook();
  }

  IDirect3DDevice9Ex *device_ex = nullptr;
  const HRESULT device_ex_hr =
      (*device)->QueryInterface(__uuidof(IDirect3DDevice9Ex),
                                reinterpret_cast<void **>(&device_ex));
  const bool is_ex_device = SUCCEEDED(device_ex_hr) && device_ex;
  if (device_ex)
    device_ex->Release();

  std::ostringstream out;
  auto **device_vtable = *reinterpret_cast<void ***>(*device);
  if (resource_census_requested() || trace_exceptions_requested() ||
      managed_texture_fallback_requested() ||
      managed_semantic_adaptation_requested()) {
    g_device_vtable_address = reinterpret_cast<std::uintptr_t>(device_vtable);
    g_device_vb_slot_address = reinterpret_cast<std::uintptr_t>(
        &device_vtable[kDeviceCreateVertexBufferIndex]);
  }
  out << "event=device_created device_id=" << state.id << " adapter=" << adapter
      << " behavior=0x" << std::hex << behavior << std::dec
      << " windowed=" << (parameters ? parameters->Windowed : 0)
      << " width=" << (parameters ? parameters->BackBufferWidth : 0)
      << " height=" << (parameters ? parameters->BackBufferHeight : 0)
      << " hook_setup=" << hooked << " device_ex=" << is_ex_device
      << " device_ptr=0x" << std::hex
      << reinterpret_cast<std::uintptr_t>(*device)
      << " device_vtable=0x" << reinterpret_cast<std::uintptr_t>(device_vtable)
      << " device_vb_slot=0x"
      << reinterpret_cast<std::uintptr_t>(
             device_vtable[kDeviceCreateVertexBufferIndex])
      << " expected_vb_hook=0x"
      << reinterpret_cast<std::uintptr_t>(&hook_create_vertex_buffer)
      << " original_vb=0x"
      << reinterpret_cast<std::uintptr_t>(g_real_create_vertex_buffer)
      << std::dec;
  if (resource_census_requested() || trace_exceptions_requested() ||
      managed_texture_fallback_requested() ||
      managed_semantic_adaptation_requested())
    out << " texture_trace_hook=" << texture_trace_hooked
        << " volume_texture_trace_hook=" << volume_texture_trace_hooked
        << " cube_texture_trace_hook=" << cube_texture_trace_hooked
        << " vertex_buffer_trace_hook=" << vertex_buffer_trace_hooked
        << " index_buffer_trace_hook=" << index_buffer_trace_hooked
        << " set_texture_trace_hook=" << set_texture_trace_hooked
        << " set_stream_source_trace_hook=" << set_stream_source_trace_hooked
        << " set_indices_trace_hook=" << set_indices_trace_hooked
        << " draw_primitive_trace_hook=" << draw_primitive_trace_hooked
        << " draw_indexed_primitive_trace_hook="
        << draw_indexed_primitive_trace_hooked
        << " vertex_declaration_trace_hook="
        << vertex_declaration_trace_hooked
        << " begin_state_block_rehook_hook=" << begin_state_block_rehook_hooked
        << " canonical_vb_hook=" << canonical_vertex_buffer_hooked;
  append_line(out.str());
  return hr;
}

IDirect3D9 *WINAPI hook_direct3d_create9(UINT sdk_version) {
  if (!g_real_create9)
    return nullptr;
  install_exception_trace();
  install_chrome_flow_probe();
  const bool promote = promote_to_d3d9ex_requested();
  IDirect3D9 *d3d9 = nullptr;
  bool promoted = false;
  HRESULT promote_hr = E_NOTIMPL;
  if (promote) {
    HMODULE d3d9_module = GetModuleHandleA("d3d9.dll");
    const auto create9ex = d3d9_module
                               ? reinterpret_cast<Direct3DCreate9ExFn>(
                                     GetProcAddress(d3d9_module,
                                                    "Direct3DCreate9Ex"))
                               : nullptr;
    IDirect3D9Ex *d3d9ex = nullptr;
    promote_hr = create9ex ? create9ex(sdk_version, &d3d9ex) : E_NOINTERFACE;
    if (SUCCEEDED(promote_hr) && d3d9ex) {
      d3d9 = d3d9ex;
      promoted = true;
    }
  }
  if (!d3d9)
    d3d9 = g_real_create9(sdk_version);
  if (d3d9) {
    const bool hooked = patch_vtable(d3d9, kD3D9CreateDeviceIndex,
                                     hook_create_device, g_real_create_device);
    std::ostringstream out;
    out << "event=direct3d9_created sdk=" << sdk_version
        << " create_device_hook=" << hooked
        << " promotion_requested=" << promote << " promoted_ex=" << promoted;
    if (promote)
      out << " promotion_hr=0x" << std::hex
          << static_cast<unsigned long>(promote_hr) << std::dec;
    append_line(out.str());
  }
  return d3d9;
}

FARPROC WINAPI hook_get_proc_address(HMODULE module, LPCSTR name) {
  if (!g_real_get_proc_address)
    return nullptr;
  FARPROC result = g_real_get_proc_address(module, name);
  if (!name || (reinterpret_cast<std::uintptr_t>(name) >> 16U) == 0U)
    return result;
  if (std::strcmp(name, "Direct3DCreate9") != 0 || !result)
    return result;

  g_real_create9 = reinterpret_cast<Direct3DCreate9Fn>(result);
  if (!g_install_logged) {
    g_install_logged = true;
    std::ostringstream install;
    install << "event=observer_installed dependency_proxy=1 engine_get_proc_address_hook="
            << g_dependency_hooked;
    append_line(install.str());
  }
  append_line("event=direct3dcreate9_resolved interception=1");
  return reinterpret_cast<FARPROC>(&hook_direct3d_create9);
}

} // namespace

extern "C" DWORD WINAPI D3DXGetShaderVersion(const DWORD *function) {
  ensure_engine_hook();
  HMODULE real = GetModuleHandleA("ltr_d3dx9_29_real.dll");
  if (!real)
    real = LoadLibraryA("ltr_d3dx9_29_real.dll");
  if (!real)
    return 0;
  const auto target = reinterpret_cast<D3DXGetShaderVersionFn>(
      GetProcAddress(real, "D3DXGetShaderVersion"));
  return target ? target(function) : 0;
}

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID) {
  if (reason == DLL_PROCESS_ATTACH) {
    DisableThreadLibraryCalls(instance);
    ensure_engine_hook();
  }
  return TRUE;
}
