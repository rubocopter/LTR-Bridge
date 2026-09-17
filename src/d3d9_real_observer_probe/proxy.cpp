#include <Windows.h>
#include <d3d9.h>
#include <d3d11.h>
#include <d3d11_4.h>
#include <dxgi1_2.h>
#include <wrl/client.h>

#include "../d3d9_real_bridge_probe/client.h"
#include "../d3d9_real_bridge_probe/protocol.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <unordered_map>

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
using ResetFn = HRESULT(STDMETHODCALLTYPE *)(IDirect3DDevice9 *,
                                             D3DPRESENT_PARAMETERS *);
using PresentFn = HRESULT(STDMETHODCALLTYPE *)(IDirect3DDevice9 *, const RECT *,
                                               const RECT *, HWND,
                                               const RGNDATA *);
using EndSceneFn = HRESULT(STDMETHODCALLTYPE *)(IDirect3DDevice9 *);

constexpr std::size_t kD3D9CreateDeviceIndex = 16;
constexpr std::size_t kDeviceReleaseIndex = 2;
constexpr std::size_t kDeviceResetIndex = 16;
constexpr std::size_t kDevicePresentIndex = 17;
constexpr std::size_t kDeviceEndSceneIndex = 42;
constexpr std::uint64_t kSummaryFrame = 120;

struct RealBridgeRuntime;

struct DeviceState {
  std::uint32_t id = 0;
  std::uint32_t resource_generation = 1;
  std::uint64_t end_scene_count = 0;
  std::uint64_t present_count = 0;
  std::uint32_t reset_count = 0;
  UINT max_swapchains = 0;
  bool first_sample_written = false;
  bool summary_written = false;
  bool render_target_observed = false;
  bool depth_observed = false;
  bool transforms_observed = false;
  bool transforms_changed = false;
  bool relay_test_attempted = false;
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
ResetFn g_real_reset = nullptr;
PresentFn g_real_present = nullptr;
EndSceneFn g_real_end_scene = nullptr;

std::mutex g_state_mutex;
std::mutex g_log_mutex;
std::unordered_map<IDirect3DDevice9 *, DeviceState> g_devices;
std::uint32_t g_next_device_id = 1;
std::wstring g_log_path;
bool g_dependency_hooked = false;
bool g_install_logged = false;

void append_line(const std::string &line);

[[nodiscard]] bool promote_to_d3d9ex_requested() {
  char value[8]{};
  const DWORD length = GetEnvironmentVariableA("LTR_D3D9_PROMOTE_EX", value,
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
        std::ostringstream out;
        out << "event=real_multiframe_bridge stage=capture_sync hr=0x"
            << std::hex << static_cast<unsigned long>(hr) << std::dec
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

  void Shutdown(const char *reason) {
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
                          (state.end_scene_count % 30u) == 0u;
  if (sample_now)
    sample_device_state(device, state);

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
  std::shared_ptr<RealBridgeRuntime> multiframe;
  {
    std::lock_guard lock(g_state_mutex);
    auto it = g_devices.find(device);
    if (it != g_devices.end())
      multiframe = std::move(it->second.multiframe);
  }
  if (multiframe)
    multiframe->Shutdown("reset_begin");

  const HRESULT hr = g_real_reset(device, parameters);
  if (SUCCEEDED(hr)) {
    std::lock_guard lock(g_state_mutex);
    auto it = g_devices.find(device);
    if (it != g_devices.end()) {
      ++it->second.reset_count;
      ++it->second.resource_generation;
      it->second.first_sample_written = false;
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

  const HRESULT hr = g_real_present(device, source, dest, window, dirty);
  if (SUCCEEDED(hr)) {
    std::lock_guard lock(g_state_mutex);
    auto it = g_devices.find(device);
    if (it != g_devices.end()) {
      DeviceState &state = it->second;
      ++state.present_count;
      if (state.end_scene_count == 0 &&
          (state.present_count <= 4 || (state.present_count % 30u) == 0u))
        sample_device_state(device, state);
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
    }
  }
  return hr;
}

HRESULT STDMETHODCALLTYPE hook_end_scene(IDirect3DDevice9 *device) {
  const HRESULT hr = g_real_end_scene(device);
  if (SUCCEEDED(hr))
    observe_scene(device);
  return hr;
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

  IDirect3DDevice9Ex *device_ex = nullptr;
  const HRESULT device_ex_hr =
      (*device)->QueryInterface(__uuidof(IDirect3DDevice9Ex),
                                reinterpret_cast<void **>(&device_ex));
  const bool is_ex_device = SUCCEEDED(device_ex_hr) && device_ex;
  if (device_ex)
    device_ex->Release();

  std::ostringstream out;
  out << "event=device_created device_id=" << state.id << " adapter=" << adapter
      << " behavior=0x" << std::hex << behavior << std::dec
      << " windowed=" << (parameters ? parameters->Windowed : 0)
      << " width=" << (parameters ? parameters->BackBufferWidth : 0)
      << " height=" << (parameters ? parameters->BackBufferHeight : 0)
      << " hook_setup=" << hooked << " device_ex=" << is_ex_device;
  append_line(out.str());
  return hr;
}

IDirect3D9 *WINAPI hook_direct3d_create9(UINT sdk_version) {
  if (!g_real_create9)
    return nullptr;
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
