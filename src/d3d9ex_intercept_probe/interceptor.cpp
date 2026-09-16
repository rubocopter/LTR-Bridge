#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <d3d11.h>
#include <d3d9.h>
#include <dxgi1_2.h>
#include <iostream>
#include <thread>
#include <windows.h>
#include <wrl/client.h>

using Microsoft::WRL::ComPtr;

namespace {

using Direct3DCreate9ExFn = HRESULT(WINAPI *)(UINT, IDirect3D9Ex **);
using CreateDeviceExFn = HRESULT(STDMETHODCALLTYPE *)(
    IDirect3D9Ex *, UINT, D3DDEVTYPE, HWND, DWORD, D3DPRESENT_PARAMETERS *,
    D3DDISPLAYMODEEX *, IDirect3DDevice9Ex **);
using EndSceneFn = HRESULT(STDMETHODCALLTYPE *)(IDirect3DDevice9Ex *);
using ResetExFn = HRESULT(STDMETHODCALLTYPE *)(IDirect3DDevice9Ex *,
                                               D3DPRESENT_PARAMETERS *,
                                               D3DDISPLAYMODEEX *);

constexpr std::size_t kD3D9ExCreateDeviceExIndex = 20;
constexpr std::size_t kDevice9EndSceneIndex = 42;
constexpr std::size_t kDevice9ExResetExIndex = 132;
constexpr std::uint32_t kExpectedFrames = 12;

Direct3DCreate9ExFn g_real_create9ex = nullptr;
CreateDeviceExFn g_create_device_ex = nullptr;
EndSceneFn g_end_scene = nullptr;
ResetExFn g_reset_ex = nullptr;

ComPtr<ID3D11Device> g_d3d11;
ComPtr<ID3D11DeviceContext> g_context11;
ComPtr<IDirect3DTexture9> g_relay9;
ComPtr<IDirect3DSurface9> g_relay_surface9;
ComPtr<ID3D11Texture2D> g_relay11;
ComPtr<ID3D11Texture2D> g_staging11;
UINT g_width = 0;
UINT g_height = 0;
D3DFORMAT g_format = D3DFMT_UNKNOWN;
std::uint32_t g_frames = 0;
std::uint32_t g_generations = 0;
std::uint32_t g_resets = 0;
std::uint64_t g_mismatches = 0;
bool g_bound_rt_observed = false;
bool g_depth_observed = false;
bool g_transform_observed = false;
bool g_state_preserved = false;
double g_capture_wall_ms = 0.0;

[[nodiscard]] bool check(HRESULT hr, const char *what) {
  if (SUCCEEDED(hr))
    return true;
  std::cerr << "intercept_" << what << " failed hr=0x" << std::hex
            << static_cast<unsigned long>(hr) << std::dec << "\n";
  return false;
}

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
  return true;
}

void release_capture_resources() {
  g_staging11.Reset();
  g_relay11.Reset();
  g_relay_surface9.Reset();
  g_relay9.Reset();
  g_width = 0;
  g_height = 0;
  g_format = D3DFMT_UNKNOWN;
}

[[nodiscard]] bool create_d3d11_for_luid(const LUID &luid) {
  ComPtr<IDXGIFactory1> factory;
  if (!check(CreateDXGIFactory1(IID_PPV_ARGS(&factory)),
             "CreateDXGIFactory1"))
    return false;

  ComPtr<IDXGIAdapter1> match;
  for (UINT i = 0;; ++i) {
    ComPtr<IDXGIAdapter1> adapter;
    const HRESULT hr = factory->EnumAdapters1(i, &adapter);
    if (hr == DXGI_ERROR_NOT_FOUND)
      break;
    if (FAILED(hr))
      return check(hr, "EnumAdapters1");
    DXGI_ADAPTER_DESC1 desc{};
    if (FAILED(adapter->GetDesc1(&desc)))
      continue;
    if (desc.AdapterLuid.LowPart == luid.LowPart &&
        desc.AdapterLuid.HighPart == luid.HighPart) {
      match = adapter;
      break;
    }
  }
  if (!match) {
    std::cerr << "intercept_reject=adapter_luid_not_found\n";
    return false;
  }

  D3D_FEATURE_LEVEL feature{};
  if (!check(D3D11CreateDevice(match.Get(), D3D_DRIVER_TYPE_UNKNOWN, nullptr, 0,
                               nullptr, 0, D3D11_SDK_VERSION, &g_d3d11,
                               &feature, &g_context11),
             "D3D11CreateDevice"))
    return false;
  std::cout << "intercept_adapter_luid_match=1\n";
  return true;
}

[[nodiscard]] bool wait_d3d9_event(IDirect3DDevice9Ex *device) {
  ComPtr<IDirect3DQuery9> query;
  if (!check(device->CreateQuery(D3DQUERYTYPE_EVENT, &query), "CreateQuery") ||
      !check(query->Issue(D3DISSUE_END), "Issue(event)"))
    return false;
  const auto deadline =
      std::chrono::steady_clock::now() + std::chrono::seconds(5);
  while (std::chrono::steady_clock::now() < deadline) {
    const HRESULT hr = query->GetData(nullptr, 0, D3DGETDATA_FLUSH);
    if (hr == S_OK)
      return true;
    if (FAILED(hr))
      return check(hr, "GetData(event)");
    std::this_thread::yield();
  }
  std::cerr << "intercept_GetData(event) timed out\n";
  return false;
}

[[nodiscard]] bool ensure_capture_resources(IDirect3DDevice9Ex *device,
                                            const D3DSURFACE_DESC &source_desc) {
  if (g_relay9 && g_width == source_desc.Width &&
      g_height == source_desc.Height && g_format == source_desc.Format)
    return true;
  release_capture_resources();
  if (source_desc.Format != D3DFMT_A2B10G10R10) {
    std::cerr << "intercept_reject=unexpected_rt_format format="
              << static_cast<unsigned>(source_desc.Format) << "\n";
    return false;
  }

  HANDLE shared_handle = nullptr;
  if (!check(device->CreateTexture(source_desc.Width, source_desc.Height, 1, 0,
                                   source_desc.Format, D3DPOOL_DEFAULT,
                                   &g_relay9, &shared_handle),
             "CreateTexture(shared-relay)") ||
      !shared_handle ||
      !check(g_relay9->GetSurfaceLevel(0, &g_relay_surface9),
             "GetSurfaceLevel(shared-relay)") ||
      !check(g_d3d11->OpenSharedResource(
                 shared_handle, __uuidof(ID3D11Texture2D),
                 reinterpret_cast<void **>(g_relay11.GetAddressOf())),
             "OpenSharedResource"))
    return false;

  D3D11_TEXTURE2D_DESC desc11{};
  g_relay11->GetDesc(&desc11);
  if (desc11.Width != source_desc.Width || desc11.Height != source_desc.Height ||
      desc11.Format != DXGI_FORMAT_R10G10B10A2_UNORM) {
    std::cerr << "intercept_reject=relay_descriptor size=" << desc11.Width
              << "x" << desc11.Height
              << " format=" << static_cast<unsigned>(desc11.Format) << "\n";
    return false;
  }
  desc11.Usage = D3D11_USAGE_STAGING;
  desc11.BindFlags = 0;
  desc11.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
  desc11.MiscFlags = 0;
  if (!check(g_d3d11->CreateTexture2D(&desc11, nullptr, &g_staging11),
             "CreateTexture2D(staging)"))
    return false;

  g_width = source_desc.Width;
  g_height = source_desc.Height;
  g_format = source_desc.Format;
  ++g_generations;
  std::cout << "intercept_relay_opened generation=" << (g_generations - 1U)
            << " size=" << g_width << "x" << g_height << "\n";
  return true;
}

[[nodiscard]] std::uint8_t r10_to_u8(std::uint32_t value) noexcept {
  return static_cast<std::uint8_t>((value * 255U + 511U) / 1023U);
}

struct Rgb {
  std::uint8_t r;
  std::uint8_t g;
  std::uint8_t b;
};

[[nodiscard]] Rgb read_r10(const std::uint8_t *pixel) noexcept {
  std::uint32_t packed = 0;
  std::memcpy(&packed, pixel, sizeof(packed));
  return {r10_to_u8(packed & 0x3FFU),
          r10_to_u8((packed >> 10U) & 0x3FFU),
          r10_to_u8((packed >> 20U) & 0x3FFU)};
}

[[nodiscard]] bool near_color(Rgb actual, Rgb expected) noexcept {
  const auto close = [](std::uint8_t a, std::uint8_t e) {
    return std::abs(static_cast<int>(a) - static_cast<int>(e)) <= 2;
  };
  return close(actual.r, expected.r) && close(actual.g, expected.g) &&
         close(actual.b, expected.b);
}

[[nodiscard]] const std::uint8_t *pixel_at(const D3D11_MAPPED_SUBRESOURCE &map,
                                           float ndc_x, float ndc_y) noexcept {
  const auto fx = (ndc_x * 0.5f + 0.5f) * static_cast<float>(g_width);
  const auto fy = (-ndc_y * 0.5f + 0.5f) * static_cast<float>(g_height);
  const UINT x =
      std::min(g_width - 1U, static_cast<UINT>(std::max(0.0f, fx)));
  const UINT y =
      std::min(g_height - 1U, static_cast<UINT>(std::max(0.0f, fy)));
  return static_cast<const std::uint8_t *>(map.pData) +
         static_cast<std::size_t>(y) * map.RowPitch +
         static_cast<std::size_t>(x) * 4U;
}

[[nodiscard]] bool validate_pixels() {
  g_context11->CopyResource(g_staging11.Get(), g_relay11.Get());
  D3D11_MAPPED_SUBRESOURCE mapped{};
  if (!check(g_context11->Map(g_staging11.Get(), 0, D3D11_MAP_READ, 0, &mapped),
             "Map(staging)"))
    return false;
  const Rgb clear_expected{20, 40, 120};
  const Rgb red_expected{220, 40, 40};
  const Rgb green_expected{40, 220, 40};
  if (!near_color(read_r10(pixel_at(mapped, -0.85f, 0.80f)), clear_expected))
    ++g_mismatches;
  if (!near_color(read_r10(pixel_at(mapped, -0.25f, 0.0f)), red_expected))
    ++g_mismatches;
  if (!near_color(read_r10(pixel_at(mapped, 0.0f, 0.0f)), red_expected))
    ++g_mismatches;
  if (!near_color(read_r10(pixel_at(mapped, 0.25f, 0.0f)), green_expected))
    ++g_mismatches;
  g_context11->Unmap(g_staging11.Get(), 0);
  return true;
}

[[nodiscard]] bool capture_after_end_scene(IDirect3DDevice9Ex *device) {
  ComPtr<IDirect3DSurface9> rt_before, depth_before;
  D3DMATRIX world_before{};
  if (!check(device->GetRenderTarget(0, &rt_before), "GetRenderTarget(before)") ||
      !check(device->GetDepthStencilSurface(&depth_before),
             "GetDepthStencilSurface(before)") ||
      !check(device->GetTransform(D3DTS_WORLD, &world_before),
             "GetTransform(before)"))
    return false;

  D3DSURFACE_DESC rt_desc{}, depth_desc{};
  if (!check(rt_before->GetDesc(&rt_desc), "GetDesc(rt)") ||
      !check(depth_before->GetDesc(&depth_desc), "GetDesc(depth)"))
    return false;
  g_bound_rt_observed = rt_desc.Format == D3DFMT_A2B10G10R10;
  g_depth_observed = depth_desc.Format == D3DFMT_D24S8 &&
                     depth_desc.Width == rt_desc.Width &&
                     depth_desc.Height == rt_desc.Height;
  const float expected_x = (g_frames & 1U) ? 0.03f : -0.03f;
  g_transform_observed = std::abs(world_before._41 - expected_x) < 0.0001f;
  if (!g_bound_rt_observed || !g_depth_observed || !g_transform_observed) {
    std::cerr << "intercept_reject=scene_state_visibility bound_rt="
              << g_bound_rt_observed << " depth=" << g_depth_observed
              << " transform=" << g_transform_observed << "\n";
    return false;
  }
  if (!ensure_capture_resources(device, rt_desc))
    return false;

  const auto start = std::chrono::steady_clock::now();
  if (!check(device->StretchRect(rt_before.Get(), nullptr, g_relay_surface9.Get(),
                                 nullptr, D3DTEXF_NONE),
             "StretchRect") ||
      !wait_d3d9_event(device))
    return false;
  g_capture_wall_ms += std::chrono::duration<double, std::milli>(
                           std::chrono::steady_clock::now() - start)
                           .count();
  if (!validate_pixels())
    return false;

  ComPtr<IDirect3DSurface9> rt_after, depth_after;
  D3DMATRIX world_after{};
  if (!check(device->GetRenderTarget(0, &rt_after), "GetRenderTarget(after)") ||
      !check(device->GetDepthStencilSurface(&depth_after),
             "GetDepthStencilSurface(after)") ||
      !check(device->GetTransform(D3DTS_WORLD, &world_after),
             "GetTransform(after)"))
    return false;
  g_state_preserved = rt_after.Get() == rt_before.Get() &&
                      depth_after.Get() == depth_before.Get() &&
                      std::abs(world_after._41 - world_before._41) < 0.0001f;
  if (!g_state_preserved) {
    std::cerr << "intercept_reject=state_not_preserved\n";
    return false;
  }

  ++g_frames;
  if (g_frames == kExpectedFrames) {
    const bool pass = g_mismatches == 0 && g_generations == 2 && g_resets == 1 &&
                      g_bound_rt_observed && g_depth_observed &&
                      g_transform_observed && g_state_preserved;
    std::cout << "intercept_frames=" << g_frames
              << " generations=" << g_generations << " resets=" << g_resets
              << " mismatches=" << g_mismatches
              << " engine_rt_observed=" << g_bound_rt_observed
              << " engine_depth_observed=" << g_depth_observed
              << " world_transform_visible=" << g_transform_observed
              << " state_preserved=" << g_state_preserved
              << " depth_occlusion_validated=" << (g_mismatches == 0)
              << " capture_cpu_wall_mean_ms="
              << (g_capture_wall_ms / static_cast<double>(g_frames)) << "\n"
              << "INTERCEPT_RESULT " << (pass ? "PASS" : "FAIL") << "\n";
    if (!pass)
      return false;
  }
  return g_mismatches == 0;
}

HRESULT STDMETHODCALLTYPE hook_end_scene(IDirect3DDevice9Ex *device) {
  const HRESULT hr = g_end_scene(device);
  if (FAILED(hr))
    return hr;
  const bool captured = capture_after_end_scene(device);
  return captured ? hr : D3DERR_INVALIDCALL;
}

HRESULT STDMETHODCALLTYPE hook_reset_ex(IDirect3DDevice9Ex *device,
                                        D3DPRESENT_PARAMETERS *pp,
                                        D3DDISPLAYMODEEX *mode) {
  release_capture_resources();
  const HRESULT hr = g_reset_ex(device, pp, mode);
  if (SUCCEEDED(hr)) {
    ++g_resets;
    std::cout << "intercept_reset_hr=0x0 count=" << g_resets << "\n";
  }
  return hr;
}

HRESULT STDMETHODCALLTYPE hook_create_device_ex(
    IDirect3D9Ex *d3d9, UINT adapter, D3DDEVTYPE device_type, HWND focus,
    DWORD behavior, D3DPRESENT_PARAMETERS *pp, D3DDISPLAYMODEEX *mode,
    IDirect3DDevice9Ex **device) {
  const HRESULT hr = g_create_device_ex(d3d9, adapter, device_type, focus,
                                        behavior, pp, mode, device);
  if (FAILED(hr) || !device || !*device)
    return hr;

  LUID luid{};
  if (!check(d3d9->GetAdapterLUID(adapter, &luid), "GetAdapterLUID")) {
    std::cerr << "intercept_reject=adapter_luid_query\n";
    return D3DERR_NOTAVAILABLE;
  }
  if (!g_d3d11) {
    if (!create_d3d11_for_luid(luid)) {
      std::cerr << "intercept_reject=d3d11_relay_device\n";
      return D3DERR_NOTAVAILABLE;
    }
  }
  if (!patch_vtable(*device, kDevice9EndSceneIndex, hook_end_scene,
                    g_end_scene) ||
      !patch_vtable(*device, kDevice9ExResetExIndex, hook_reset_ex, g_reset_ex)) {
    std::cerr << "intercept_reject=device_hook_setup\n";
    return D3DERR_NOTAVAILABLE;
  }
  std::cout << "intercept_device_hooked=1\n" << std::flush;
  return hr;
}

HRESULT WINAPI hook_direct3d_create9ex(UINT sdk_version, IDirect3D9Ex **d3d9) {
  const HRESULT hr = g_real_create9ex(sdk_version, d3d9);
  if (SUCCEEDED(hr) && d3d9 && *d3d9 &&
      !patch_vtable(*d3d9, kD3D9ExCreateDeviceExIndex, hook_create_device_ex,
                    g_create_device_ex)) {
    std::cerr << "intercept_reject=create_device_hook_setup\n";
    return E_FAIL;
  }
  return hr;
}

[[nodiscard]] bool patch_main_iat() {
  auto *base = reinterpret_cast<std::uint8_t *>(GetModuleHandleW(nullptr));
  if (!base)
    return false;
  const auto *dos = reinterpret_cast<const IMAGE_DOS_HEADER *>(base);
  if (dos->e_magic != IMAGE_DOS_SIGNATURE)
    return false;
  const auto *nt =
      reinterpret_cast<const IMAGE_NT_HEADERS32 *>(base + dos->e_lfanew);
  if (nt->Signature != IMAGE_NT_SIGNATURE)
    return false;
  const auto &dir =
      nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
  if (!dir.VirtualAddress)
    return false;

  auto *desc =
      reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR *>(base + dir.VirtualAddress);
  for (; desc->Name; ++desc) {
    const char *module_name = reinterpret_cast<const char *>(base + desc->Name);
    if (_stricmp(module_name, "d3d9.dll") != 0)
      continue;
    auto *names = reinterpret_cast<IMAGE_THUNK_DATA32 *>(
        base + (desc->OriginalFirstThunk ? desc->OriginalFirstThunk
                                        : desc->FirstThunk));
    auto *slots =
        reinterpret_cast<IMAGE_THUNK_DATA32 *>(base + desc->FirstThunk);
    for (; names->u1.AddressOfData; ++names, ++slots) {
      if (IMAGE_SNAP_BY_ORDINAL32(names->u1.Ordinal))
        continue;
      const auto *import_name = reinterpret_cast<const IMAGE_IMPORT_BY_NAME *>(
          base + names->u1.AddressOfData);
      if (std::strcmp(reinterpret_cast<const char *>(import_name->Name),
                      "Direct3DCreate9Ex") != 0)
        continue;

      auto *slot = &slots->u1.Function;
      g_real_create9ex = reinterpret_cast<Direct3DCreate9ExFn>(
          static_cast<std::uintptr_t>(*slot));
      DWORD old_protect = 0;
      if (!VirtualProtect(slot, sizeof(*slot), PAGE_READWRITE, &old_protect))
        return false;
      *slot = static_cast<DWORD>(
          reinterpret_cast<std::uintptr_t>(&hook_direct3d_create9ex));
      DWORD ignored = 0;
      VirtualProtect(slot, sizeof(*slot), old_protect, &ignored);
      FlushInstructionCache(GetCurrentProcess(), slot, sizeof(*slot));
      return g_real_create9ex != nullptr;
    }
  }
  return false;
}

} // namespace

extern "C" __declspec(dllexport) BOOL WINAPI LtrInstallD3D9ExInterceptor() {
  const bool patched = patch_main_iat();
  std::cout << "intercept_install_iat=" << (patched ? 1 : 0) << "\n"
            << std::flush;
  return patched ? TRUE : FALSE;
}

extern "C" __declspec(dllexport) void WINAPI LtrShutdownD3D9ExInterceptor() {
  release_capture_resources();
  g_context11.Reset();
  g_d3d11.Reset();
}

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID) {
  if (reason == DLL_PROCESS_ATTACH)
    DisableThreadLibraryCalls(instance);
  return TRUE;
}
