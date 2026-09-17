#include "client.h"

#include "protocol.h"

#include <Windows.h>
#include <d3d11_4.h>
#include <dxgi1_2.h>
#include <wrl/client.h>

#include <array>
#include <limits>
#include <sstream>
#include <string_view>
#include <vector>

namespace ltr::d3d9_real_bridge {
namespace {

using Microsoft::WRL::ComPtr;

[[nodiscard]] std::uint64_t pack_luid(const LUID &luid) noexcept {
  return (static_cast<std::uint64_t>(static_cast<std::uint32_t>(luid.HighPart))
          << 32U) |
         static_cast<std::uint64_t>(luid.LowPart);
}

[[nodiscard]] std::wstring quote(const std::wstring &value) {
  return L"\"" + value + L"\"";
}

void close_handle(HANDLE &handle) {
  if (handle) {
    CloseHandle(handle);
    handle = nullptr;
  }
}

[[nodiscard]] std::string read_text_file(const std::wstring &path) {
  HANDLE file = CreateFileW(path.c_str(), GENERIC_READ,
                            FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                            OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (file == INVALID_HANDLE_VALUE)
    return {};
  LARGE_INTEGER size{};
  std::string text;
  if (GetFileSizeEx(file, &size) && size.QuadPart > 0 &&
      size.QuadPart < 256 * 1024) {
    text.resize(static_cast<std::size_t>(size.QuadPart));
    DWORD read = 0;
    if (!ReadFile(file, text.data(), static_cast<DWORD>(text.size()), &read,
                  nullptr))
      text.clear();
    else
      text.resize(read);
  }
  CloseHandle(file);
  while (!text.empty() && (text.back() == '\r' || text.back() == '\n'))
    text.pop_back();
  return text;
}

[[nodiscard]] bool read_bootstrap(HANDLE pipe, HANDLE process,
                                  BootstrapMessage &out,
                                  std::uint32_t timeout_ms) {
  const ULONGLONG deadline = GetTickCount64() + timeout_ms;
  while (GetTickCount64() < deadline) {
    DWORD available = 0;
    if (!PeekNamedPipe(pipe, nullptr, 0, nullptr, &available, nullptr))
      return false;
    if (available >= sizeof(out)) {
      DWORD read = 0;
      return ReadFile(pipe, &out, sizeof(out), &read, nullptr) &&
             read == sizeof(out);
    }
    if (WaitForSingleObject(process, 0) == WAIT_OBJECT_0)
      return false;
    Sleep(1);
  }
  return false;
}

[[nodiscard]] bool resource_contract(ID3D11Texture2D *texture,
                                     std::uint32_t width,
                                     std::uint32_t height) {
  if (!texture)
    return false;
  D3D11_TEXTURE2D_DESC desc{};
  texture->GetDesc(&desc);
  return desc.Width == width && desc.Height == height && desc.MipLevels == 1 &&
         desc.ArraySize == 1 && desc.Format == DXGI_FORMAT_B8G8R8A8_UNORM &&
         desc.SampleDesc.Count == 1;
}

} // namespace

struct Client::Impl {
  ClientOptions options{};
  Logger logger;
  ComPtr<ID3D11Device> device;
  ComPtr<ID3D11Device1> device1;
  ComPtr<ID3D11Device5> device5;
  ComPtr<ID3D11DeviceContext> context;
  ComPtr<ID3D11DeviceContext4> context4;
  std::array<ComPtr<ID3D11Texture2D>, kRingDepth> transport;
  ComPtr<ID3D11Fence> ready_fence;
  ComPtr<ID3D11Fence> done_fence;
  HANDLE ready_shared_handle = nullptr;
  HANDLE process = nullptr;
  std::wstring ready_fence_name;
  std::uint32_t submitted = 0;
  std::uint32_t backpressure_checks = 0;
  std::uint32_t child_liveness_checks = 0;
  std::uint64_t adapter_luid = 0;
  DWORD child_exit = std::numeric_limits<DWORD>::max();
  bool active = false;
  bool finished = false;
  bool passed = false;

  void Log(const std::string &line) const {
    if (logger)
      logger(line);
  }

  void ReleaseGpuObjects() {
    transport = {};
    ready_fence.Reset();
    done_fence.Reset();
    context4.Reset();
    context.Reset();
    device5.Reset();
    device1.Reset();
    device.Reset();
    close_handle(ready_shared_handle);
  }

  [[nodiscard]] bool FinishIfExited() {
    if (!process || finished)
      return finished;
    if (WaitForSingleObject(process, 0) != WAIT_OBJECT_0)
      return false;
    GetExitCodeProcess(process, &child_exit);
    close_handle(process);
    const std::string child_log = read_text_file(options.log_path);
    if (!child_log.empty())
      Log(child_log);
    DeleteFileW(options.log_path.c_str());
    finished = true;
    active = false;
    passed = child_exit == 0 && submitted == options.frames &&
             done_fence && done_fence->GetCompletedValue() >= options.frames;
    std::ostringstream out;
    out << "event=real_multiframe_client generation=" << options.generation
        << " frames_submitted=" << submitted
        << " backpressure_checks=" << backpressure_checks
        << " child_liveness_checks=" << child_liveness_checks
        << " ready_completed="
        << (ready_fence ? ready_fence->GetCompletedValue() : 0)
        << " done_completed="
        << (done_fence ? done_fence->GetCompletedValue() : 0)
        << " child_exit=" << child_exit << " RESULT "
        << (passed ? "PASS" : "FAIL");
    Log(out.str());
    return true;
  }
};

Client::Client() : impl_(std::make_unique<Impl>()) {}

Client::~Client() { Shutdown(true); }

bool Client::Start(ID3D11Device *device, ID3D11DeviceContext *context,
                   const ClientOptions &options, Logger logger) {
  Shutdown(true);
  impl_ = std::make_unique<Impl>();
  Impl &state = *impl_;
  state.options = options;
  state.logger = std::move(logger);
  if (!device || !context || options.sink_path.empty() ||
      options.log_path.empty() || !options.width || !options.height ||
      options.frames < kRingDepth) {
    state.Log("event=real_multiframe_client stage=arguments RESULT FAIL");
    return false;
  }

  state.device = device;
  state.context = context;
  if (FAILED(state.device.As(&state.device1)) ||
      FAILED(state.device.As(&state.device5)) ||
      FAILED(state.context.As(&state.context4))) {
    state.Log("event=real_multiframe_client stage=interfaces RESULT FAIL");
    state.ReleaseGpuObjects();
    return false;
  }

  ComPtr<IDXGIDevice> dxgi_device;
  ComPtr<IDXGIAdapter> adapter;
  DXGI_ADAPTER_DESC adapter_desc{};
  if (FAILED(state.device.As(&dxgi_device)) ||
      FAILED(dxgi_device->GetAdapter(&adapter)) ||
      FAILED(adapter->GetDesc(&adapter_desc))) {
    state.Log("event=real_multiframe_client stage=adapter RESULT FAIL");
    state.ReleaseGpuObjects();
    return false;
  }
  state.adapter_luid = pack_luid(adapter_desc.AdapterLuid);

  std::wostringstream fence_name;
  fence_name << L"Local\\LTRD3D9RealReady_" << GetCurrentProcessId() << L'_'
             << options.generation << L'_' << GetTickCount64();
  state.ready_fence_name = fence_name.str();
  HRESULT hr = state.device5->CreateFence(0, D3D11_FENCE_FLAG_SHARED,
                                          IID_PPV_ARGS(&state.ready_fence));
  if (SUCCEEDED(hr))
    hr = state.ready_fence->CreateSharedHandle(
        nullptr, GENERIC_ALL, state.ready_fence_name.c_str(),
        &state.ready_shared_handle);
  if (FAILED(hr) || !state.ready_shared_handle) {
    state.Log("event=real_multiframe_client stage=ready_fence RESULT FAIL");
    state.ReleaseGpuObjects();
    return false;
  }

  SECURITY_ATTRIBUTES security{};
  security.nLength = sizeof(security);
  security.bInheritHandle = TRUE;
  HANDLE bootstrap_read = nullptr;
  HANDLE bootstrap_write = nullptr;
  if (!CreatePipe(&bootstrap_read, &bootstrap_write, &security, 0) ||
      !SetHandleInformation(bootstrap_read, HANDLE_FLAG_INHERIT, 0)) {
    state.Log("event=real_multiframe_client stage=bootstrap_pipe RESULT FAIL");
    close_handle(bootstrap_read);
    close_handle(bootstrap_write);
    state.ReleaseGpuObjects();
    return false;
  }

  DeleteFileW(options.log_path.c_str());
  std::wostringstream command;
  command << quote(options.sink_path) << L" --bootstrap-write "
          << static_cast<unsigned long long>(
                 reinterpret_cast<std::uintptr_t>(bootstrap_write))
          << L" --parent-pid " << GetCurrentProcessId()
          << L" --ready-fence-name " << quote(state.ready_fence_name)
          << L" --protocol-version " << kProtocolVersion << L" --adapter-luid "
          << state.adapter_luid << L" --frames " << options.frames
          << L" --generation " << options.generation
          << L" --width " << options.width << L" --height " << options.height
          << L" --initial-stall-ms " << options.initial_stall_ms
          << L" --validate-synthetic-pattern "
          << (options.validate_synthetic_pattern ? 1 : 0) << L" --log "
          << quote(options.log_path);
  std::wstring command_line = command.str();
  SIZE_T attribute_bytes = 0;
  InitializeProcThreadAttributeList(nullptr, 1, 0, &attribute_bytes);
  std::vector<std::byte> attribute_storage(attribute_bytes);
  STARTUPINFOEXW startup{};
  startup.StartupInfo.cb = sizeof(startup);
  startup.lpAttributeList = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(
      attribute_storage.data());
  HANDLE inherited_handles[] = {bootstrap_write};
  if (!attribute_bytes ||
      !InitializeProcThreadAttributeList(startup.lpAttributeList, 1, 0,
                                         &attribute_bytes) ||
      !UpdateProcThreadAttribute(
          startup.lpAttributeList, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST,
          inherited_handles, sizeof(inherited_handles), nullptr, nullptr)) {
    state.Log("event=real_multiframe_client stage=process_attributes RESULT FAIL");
    if (startup.lpAttributeList)
      DeleteProcThreadAttributeList(startup.lpAttributeList);
    close_handle(bootstrap_read);
    close_handle(bootstrap_write);
    state.ReleaseGpuObjects();
    return false;
  }
  PROCESS_INFORMATION process{};
  const BOOL launched = CreateProcessW(
      nullptr, command_line.data(), nullptr, nullptr, TRUE,
      CREATE_NO_WINDOW | EXTENDED_STARTUPINFO_PRESENT, nullptr, nullptr,
      &startup.StartupInfo, &process);
  DeleteProcThreadAttributeList(startup.lpAttributeList);
  close_handle(bootstrap_write);
  if (!launched) {
    std::ostringstream out;
    out << "event=real_multiframe_client stage=create_process error="
        << GetLastError() << " RESULT FAIL";
    state.Log(out.str());
    close_handle(bootstrap_read);
    state.ReleaseGpuObjects();
    return false;
  }
  CloseHandle(process.hThread);
  state.process = process.hProcess;

  BootstrapMessage bootstrap{};
  if (!read_bootstrap(bootstrap_read, state.process, bootstrap,
                      options.bootstrap_timeout_ms)) {
    state.Log("event=real_multiframe_client stage=bootstrap_read RESULT FAIL");
    close_handle(bootstrap_read);
    Shutdown(true);
    return false;
  }
  close_handle(bootstrap_read);
  if (bootstrap.magic != kBootstrapMagic ||
      bootstrap.protocol != kProtocolVersion ||
      bootstrap.width != options.width || bootstrap.height != options.height ||
      bootstrap.frames != options.frames || bootstrap.ring_depth != kRingDepth ||
      bootstrap.adapter_luid != state.adapter_luid ||
      !bootstrap.resource0_handle || !bootstrap.resource1_handle ||
      !bootstrap.done_fence_handle) {
    state.Log("event=real_multiframe_client stage=bootstrap_contract RESULT FAIL");
    Shutdown(true);
    return false;
  }

  std::array<HANDLE, kRingDepth> resource_handles = {
      reinterpret_cast<HANDLE>(
          static_cast<std::uintptr_t>(bootstrap.resource0_handle)),
      reinterpret_cast<HANDLE>(
          static_cast<std::uintptr_t>(bootstrap.resource1_handle))};
  for (std::uint32_t slot = 0; slot < kRingDepth; ++slot) {
    const HRESULT open = state.device1->OpenSharedResource1(
        resource_handles[slot], IID_PPV_ARGS(&state.transport[slot]));
    close_handle(resource_handles[slot]);
    if (FAILED(open) || !resource_contract(state.transport[slot].Get(),
                                           options.width, options.height)) {
      std::ostringstream out;
      out << "event=real_multiframe_client stage=open_transport slot=" << slot
          << " hr=0x" << std::hex << static_cast<unsigned long>(open)
          << " RESULT FAIL";
      state.Log(out.str());
      Shutdown(true);
      return false;
    }
  }

  HANDLE done_handle = reinterpret_cast<HANDLE>(
      static_cast<std::uintptr_t>(bootstrap.done_fence_handle));
  const HRESULT open_done = state.device5->OpenSharedFence(
      done_handle, __uuidof(ID3D11Fence),
      reinterpret_cast<void **>(state.done_fence.GetAddressOf()));
  close_handle(done_handle);
  if (FAILED(open_done) || !state.done_fence) {
    state.Log("event=real_multiframe_client stage=open_done_fence RESULT FAIL");
    Shutdown(true);
    return false;
  }

  state.active = true;
  std::ostringstream out;
  out << "event=real_multiframe_client stage=initialized protocol="
      << kProtocolVersion << " generation=" << options.generation
      << " ring_depth=" << kRingDepth << " frames=" << options.frames
      << " width=" << options.width << " height=" << options.height
      << " adapter_luid=" << state.adapter_luid;
  state.Log(out.str());
  return true;
}

SubmitStatus Client::QuerySubmitStatus() {
  Impl &state = *impl_;
  if (state.finished)
    return state.passed ? SubmitStatus::finished : SubmitStatus::failed;
  if (!state.active || !state.process)
    return SubmitStatus::failed;
  if (state.FinishIfExited())
    return state.passed ? SubmitStatus::finished : SubmitStatus::failed;
  ++state.child_liveness_checks;
  if (state.submitted >= state.options.frames)
    return SubmitStatus::waiting_for_completion;
  const std::uint64_t reuse_target = reuse_done_value(state.submitted);
  if (reuse_target && state.done_fence->GetCompletedValue() < reuse_target) {
    ++state.backpressure_checks;
    return SubmitStatus::backpressure;
  }
  return SubmitStatus::submitted;
}

SubmitStatus Client::TrySubmit(ID3D11Texture2D *source) {
  Impl &state = *impl_;
  if (!source)
    return SubmitStatus::failed;
  const SubmitStatus available = QuerySubmitStatus();
  if (available != SubmitStatus::submitted)
    return available;
  if (!resource_contract(source, state.options.width, state.options.height)) {
    state.Log("event=real_multiframe_client stage=source_contract RESULT FAIL");
    Shutdown(true);
    return SubmitStatus::failed;
  }

  const std::uint32_t frame = state.submitted;
  const std::uint32_t slot = frame % kRingDepth;
  state.context->CopyResource(state.transport[slot].Get(), source);
  const std::uint64_t ready = ready_value(frame);
  const HRESULT signal = state.context4->Signal(state.ready_fence.Get(), ready);
  state.context->Flush();
  if (FAILED(signal)) {
    std::ostringstream out;
    out << "event=real_multiframe_client stage=signal_ready frame=" << frame
        << " hr=0x" << std::hex << static_cast<unsigned long>(signal)
        << " RESULT FAIL";
    state.Log(out.str());
    Shutdown(true);
    return SubmitStatus::failed;
  }
  ++state.submitted;
  if (state.submitted <= 3 || state.submitted == state.options.frames) {
    std::ostringstream out;
    out << "event=real_multiframe_client frame_submitted=" << state.submitted
        << " generation=" << state.options.generation << " slot=" << slot
        << " ready_completed=" << state.ready_fence->GetCompletedValue()
        << " done_completed=" << state.done_fence->GetCompletedValue();
    state.Log(out.str());
  }
  return SubmitStatus::submitted;
}

bool Client::Poll() {
  Impl &state = *impl_;
  if (state.finished)
    return true;
  return state.FinishIfExited();
}

void Client::Shutdown(bool terminate_child) {
  if (!impl_)
    return;
  Impl &state = *impl_;
  if (state.process) {
    if (!state.FinishIfExited() && terminate_child &&
        WaitForSingleObject(state.process, 0) == WAIT_TIMEOUT) {
      TerminateProcess(state.process, 90);
      WaitForSingleObject(state.process, 1000);
      (void)state.FinishIfExited();
    }
    close_handle(state.process);
  }
  state.active = false;
  state.ReleaseGpuObjects();
}

ClientSnapshot Client::Snapshot() const {
  const Impl &state = *impl_;
  ClientSnapshot out{};
  out.submitted = state.submitted;
  out.backpressure_checks = state.backpressure_checks;
  out.child_liveness_checks = state.child_liveness_checks;
  out.ready_completed =
      state.ready_fence ? state.ready_fence->GetCompletedValue() : 0;
  out.done_completed =
      state.done_fence ? state.done_fence->GetCompletedValue() : 0;
  out.adapter_luid = state.adapter_luid;
  out.child_exit = state.child_exit;
  out.active = state.active;
  out.finished = state.finished;
  out.passed = state.passed;
  return out;
}

} // namespace ltr::d3d9_real_bridge
