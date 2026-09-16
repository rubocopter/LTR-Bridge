#include "common.h"
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <d3d12.h>
#include <d3dcompiler.h>
#include <iomanip>
#include <iostream>
#include <limits>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>
#include <windows.h>
#include <wrl/client.h>
using Microsoft::WRL::ComPtr;
namespace {
[[nodiscard]] bool check(HRESULT hr, const char *what) {
  if (SUCCEEDED(hr))
    return true;
  std::cerr << what << " failed hr=0x" << std::hex
            << static_cast<unsigned long>(hr) << std::dec << "\n";
  return false;
}
[[nodiscard]] std::wstring quote(const std::wstring &v) {
  return L"\"" + v + L"\"";
}
struct Options {
  std::wstring producer, negative;
  std::uint32_t backpressure_depth = 0;
  std::uint32_t renderer_copy_mode = 0;
  std::wstring renderer_profile;
  bool stereo = false;
  bool stereo_contamination = false;
  bool stereo_history = false;
  bool stereo_history_swap = false;
};
[[nodiscard]] bool parse(int argc, wchar_t **argv, Options &o) {
  for (int i = 1; i < argc; ++i) {
    const std::wstring_view k(argv[i]);
    if (k == L"--producer" && i + 1 < argc)
      o.producer = argv[++i];
    else if (k == L"--negative" && i + 1 < argc)
      o.negative = argv[++i];
    else if (k == L"--backpressure-depth" && i + 1 < argc) {
      try {
        const auto depth = std::stoul(argv[++i]);
        if (depth < 1U || depth > 2U)
          return false;
        o.backpressure_depth = depth;
      } catch (...) {
        return false;
      }
    } else if (k == L"--stereo") {
      o.stereo = true;
    } else if (k == L"--stereo-contamination") {
      o.stereo = true;
      o.stereo_contamination = true;
    } else if (k == L"--stereo-history") {
      o.stereo = true;
      o.stereo_history = true;
    } else if (k == L"--stereo-history-swap") {
      o.stereo = true;
      o.stereo_history = true;
      o.stereo_history_swap = true;
    } else if (k == L"--renderer-copy") {
      if (o.renderer_copy_mode)
        return false;
      o.renderer_copy_mode = 1U;
      o.renderer_profile = L"tiny-copy";
    } else if (k == L"--renderer-copy-highres") {
      if (o.renderer_copy_mode)
        return false;
      o.renderer_copy_mode = 1U;
      o.renderer_profile = L"highres-copy";
    } else if (k == L"--renderer-resolve-msaa4x") {
      if (o.renderer_copy_mode)
        return false;
      o.renderer_copy_mode = 2U;
      o.renderer_profile = L"msaa4x-resolve";
    } else if (k == L"--renderer-convert-r10") {
      if (o.renderer_copy_mode)
        return false;
      o.renderer_copy_mode = 3U;
      o.renderer_profile = L"r10-to-rgba8";
    } else if (k == L"--d3d9ex-relay") {
      if (o.renderer_copy_mode)
        return false;
      o.renderer_copy_mode = 4U;
      o.renderer_profile = L"d3d9ex-r10-relay";
    } else if (k == L"--d3d9ex-relay-highres") {
      if (o.renderer_copy_mode)
        return false;
      o.renderer_copy_mode = 4U;
      o.renderer_profile = L"d3d9ex-r10-relay-highres";
    } else
      return false;
  }
  return !o.producer.empty() &&
         (o.negative.empty() || o.backpressure_depth == 0U) &&
         (!o.stereo || (o.negative.empty() && o.backpressure_depth == 0U)) &&
         (!o.renderer_copy_mode ||
          (o.negative.empty() && o.backpressure_depth == 0U && !o.stereo));
}
[[nodiscard]] ComPtr<ID3D12Resource>
texture(ID3D12Device *d, std::uint32_t w, std::uint32_t h,
        DXGI_FORMAT format = DXGI_FORMAT_R8G8B8A8_UNORM) {
  D3D12_HEAP_PROPERTIES hp{};
  hp.Type = D3D12_HEAP_TYPE_DEFAULT;
  D3D12_RESOURCE_DESC r{};
  r.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
  r.Width = w;
  r.Height = h;
  r.DepthOrArraySize = 1;
  r.MipLevels = 1;
  r.Format = format;
  r.SampleDesc.Count = 1;
  r.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
  r.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET |
            D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS |
            D3D12_RESOURCE_FLAG_ALLOW_SIMULTANEOUS_ACCESS;
  ComPtr<ID3D12Resource> out;
  if (!check(d->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_SHARED, &r,
                                        D3D12_RESOURCE_STATE_COMMON, nullptr,
                                        IID_PPV_ARGS(&out)),
             "CreateCommittedResource(shared)"))
    return nullptr;
  return out;
}
[[nodiscard]] DWORD wait_process(PROCESS_INFORMATION &p, DWORD ms) {
  if (WaitForSingleObject(p.hProcess, ms) != WAIT_OBJECT_0)
    return std::numeric_limits<DWORD>::max();
  DWORD code = 0;
  if (!GetExitCodeProcess(p.hProcess, &code))
    return std::numeric_limits<DWORD>::max();
  return code;
}
} // namespace
int wmain(int argc, wchar_t **argv) {
  static_assert(sizeof(void *) == 8, "consumer must be built x64");
  Options o{};
  if (!parse(argc, argv, o)) {
    std::cerr
        << "usage: consumer --producer <x86-producer> [--negative "
           "protocol|adapter|resource-contract|host-stall|dynamic-control|"
           "format-rgba16f|format-rgba32f|format-rgba8uint|client-termination|"
           "device-removal|host-termination|"
           "backpressure-host-termination]"
           " [--backpressure-depth 1|2] [--stereo|--stereo-contamination|"
           "--stereo-history|--stereo-history-swap] [--renderer-copy|"
           "--renderer-copy-highres|--renderer-resolve-msaa4x|"
           "--renderer-convert-r10|--d3d9ex-relay|"
           "--d3d9ex-relay-highres]\n";
    return 2;
  }
  if (o.negative == L"backpressure-host-termination")
    o.backpressure_depth = 1U;
  ComPtr<ID3D12Device> dev;
  if (!check(D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0,
                               IID_PPV_ARGS(&dev)),
             "D3D12CreateDevice"))
    return 3;
  LUID luid = dev->GetAdapterLuid();
  D3D12_COMMAND_QUEUE_DESC qd{};
  qd.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
  ComPtr<ID3D12CommandQueue> queue;
  if (!check(dev->CreateCommandQueue(&qd, IID_PPV_ARGS(&queue)),
             "CreateCommandQueue"))
    return 4;
  auto specs = std::vector<ltr::bridge_probe::GenerationSpec>(
      std::begin(ltr::bridge_probe::kGenerations),
      std::end(ltr::bridge_probe::kGenerations));
  if (o.renderer_profile == L"highres-copy") {
    specs[0] = {1920, 1080};
    specs[1] = {3840, 2160};
  } else if (o.renderer_profile == L"msaa4x-resolve" ||
             o.renderer_profile == L"r10-to-rgba8") {
    specs[0] = {1920, 1080};
    specs[1] = {2560, 1440};
  } else if (o.renderer_profile == L"d3d9ex-r10-relay-highres") {
    specs[0] = {1920, 1080};
    specs[1] = {2560, 1440};
  }
  ComPtr<ID3D12Resource> res[ltr::bridge_probe::kGenerationCount];
  HANDLE rh[ltr::bridge_probe::kGenerationCount]{};
  SECURITY_ATTRIBUTES sa{};
  sa.nLength = sizeof(sa);
  sa.bInheritHandle = TRUE;
  DXGI_FORMAT resource_format = DXGI_FORMAT_R8G8B8A8_UNORM;
  if (o.negative == L"format-rgba16f")
    resource_format = DXGI_FORMAT_R16G16B16A16_FLOAT;
  else if (o.negative == L"format-rgba32f")
    resource_format = DXGI_FORMAT_R32G32B32A32_FLOAT;
  else if (o.negative == L"format-rgba8uint")
    resource_format = DXGI_FORMAT_R8G8B8A8_UINT;
  res[0] = texture(dev.Get(), specs[0].width, specs[0].height, resource_format);
  if (!res[0])
    return 5;
  if (!check(dev->CreateSharedHandle(res[0].Get(), &sa, GENERIC_ALL, nullptr,
                                     &rh[0]),
             "CreateSharedHandle(resource0)"))
    return 6;
  if (o.backpressure_depth == 2U || o.stereo) {
    res[1] = texture(dev.Get(), specs[0].width, specs[0].height);
    if (!res[1])
      return 5;
    if (!check(dev->CreateSharedHandle(res[1].Get(), &sa, GENERIC_ALL, nullptr,
                                       &rh[1]),
               "CreateSharedHandle(resource1)"))
      return 6;
  }
  HANDLE control_read = nullptr, control_write = nullptr;
  if (!CreatePipe(&control_read, &control_write, &sa, 0)) {
    std::cerr << "CreatePipe(control) failed error=" << GetLastError() << "\n";
    return 6;
  }
  HANDLE host_process_handle = nullptr;
  if (!DuplicateHandle(GetCurrentProcess(), GetCurrentProcess(),
                       GetCurrentProcess(), &host_process_handle, SYNCHRONIZE,
                       TRUE, 0)) {
    std::cerr << "DuplicateHandle(host-process) failed error=" << GetLastError()
              << "\n";
    CloseHandle(control_read);
    CloseHandle(control_write);
    return 6;
  }
  if (!SetHandleInformation(control_write, HANDLE_FLAG_INHERIT, 0)) {
    std::cerr << "SetHandleInformation(control-write) failed error="
              << GetLastError() << "\n";
    CloseHandle(control_read);
    CloseHandle(control_write);
    return 6;
  }
  ComPtr<ID3D12Fence> done_fence;
  if (!check(dev->CreateFence(0, D3D12_FENCE_FLAG_SHARED,
                              IID_PPV_ARGS(&done_fence)),
             "CreateFence(done)"))
    return 6;
  HANDLE done_fence_handle = nullptr;
  if (!check(dev->CreateSharedHandle(done_fence.Get(), &sa, GENERIC_ALL,
                                     nullptr, &done_fence_handle),
             "CreateSharedHandle(done-fence)"))
    return 6;
  ComPtr<ID3D12Fence> stereo_done_fence;
  HANDLE stereo_done_fence_handle = nullptr;
  if (o.stereo) {
    if (!check(dev->CreateFence(0, D3D12_FENCE_FLAG_SHARED,
                                IID_PPV_ARGS(&stereo_done_fence)),
               "CreateFence(stereo-done1)"))
      return 6;
    if (!check(dev->CreateSharedHandle(stereo_done_fence.Get(), &sa,
                                       GENERIC_ALL, nullptr,
                                       &stereo_done_fence_handle),
               "CreateSharedHandle(stereo-done1)"))
      return 6;
  }
  std::uint32_t protocol = ltr::bridge_probe::kProtocolVersion;
  DWORD expected = 0;
  bool deferred_negative = false;
  std::uint32_t expect_host_stall = 0;
  std::uint32_t expect_host_termination = 0;
  std::uint32_t expect_backpressure_host_termination = 0;
  if (o.negative == L"protocol") {
    ++protocol;
    expected = 3;
  } else if (o.negative == L"adapter") {
    luid.LowPart ^= 0x7FFFFFFFU;
    expected = 5;
  } else if (o.negative == L"resource-contract") {
    ++specs[0].width;
    expected = 9;
  } else if (o.negative == L"format-rgba32f") {
    // On the current host D3D11 rejects the D3D12 shared resource before the
    // producer can inspect its descriptor, so this is an interop-open negative.
    expected = 8;
  } else if (o.negative == L"format-rgba16f" ||
             o.negative == L"format-rgba8uint") {
    expected = 9;
  } else if (o.negative == L"host-stall") {
    expect_host_stall = 1;
    expected = 17;
  } else if (o.negative == L"dynamic-control") {
    expected = 19;
    deferred_negative = true;
  } else if (o.negative == L"client-termination") {
    expected = 21;
    deferred_negative = true;
  } else if (o.negative == L"device-removal") {
    expected = 19;
    deferred_negative = true;
  } else if (o.negative == L"host-termination") {
    expect_host_termination = 1;
  } else if (o.negative == L"backpressure-host-termination") {
    expect_backpressure_host_termination = 1;
  } else if (!o.negative.empty()) {
    std::cerr << "unknown negative mode\n";
    return 7;
  }
  const std::wstring ready_fence_name =
      L"Local\\LTRBridgeReadyFence_" + std::to_wstring(GetCurrentProcessId());
  const std::wstring stereo_ready_fence_name = ready_fence_name + L"_Right";
  std::wostringstream cmd;
  cmd << quote(o.producer) << L" --resource0-handle "
      << static_cast<unsigned long long>(
             reinterpret_cast<std::uintptr_t>(rh[0]));
  if (o.backpressure_depth == 2U || o.stereo)
    cmd << L" --resource1-handle "
        << static_cast<unsigned long long>(
               reinterpret_cast<std::uintptr_t>(rh[1]));
  cmd << L" --done-fence-handle "
      << static_cast<unsigned long long>(
             reinterpret_cast<std::uintptr_t>(done_fence_handle))
      << L" --done-fence1-handle "
      << static_cast<unsigned long long>(
             reinterpret_cast<std::uintptr_t>(stereo_done_fence_handle))
      << L" --control-read-handle "
      << static_cast<unsigned long long>(
             reinterpret_cast<std::uintptr_t>(control_read))
      << L" --host-process-handle "
      << static_cast<unsigned long long>(
             reinterpret_cast<std::uintptr_t>(host_process_handle))
      << L" --width0 " << specs[0].width << L" --height0 " << specs[0].height
      << L" --ready-fence-name " << quote(ready_fence_name)
      << L" --ready-fence1-name " << quote(stereo_ready_fence_name)
      << L" --luid-low " << luid.LowPart << L" --luid-high " << luid.HighPart
      << L" --protocol-version " << protocol << L" --frames-per-generation "
      << ltr::bridge_probe::kFramesPerGeneration << L" --expect-host-stall "
      << expect_host_stall << L" --expect-host-termination "
      << expect_host_termination << L" --expect-backpressure-host-termination "
      << expect_backpressure_host_termination << L" --backpressure-depth "
      << o.backpressure_depth << L" --stereo-mode " << (o.stereo ? 1U : 0U)
      << L" --stereo-history-mode " << (o.stereo_history ? 1U : 0U)
      << L" --renderer-copy-mode " << o.renderer_copy_mode;
  std::wstring line = cmd.str();
  STARTUPINFOW si{};
  si.cb = sizeof(si);
  PROCESS_INFORMATION pi{};
  const auto start = std::chrono::steady_clock::now();
  if (!CreateProcessW(nullptr, line.data(), nullptr, nullptr, TRUE, 0, nullptr,
                      nullptr, &si, &pi)) {
    std::cerr << "CreateProcessW failed error=" << GetLastError() << "\n";
    CloseHandle(control_read);
    CloseHandle(control_write);
    CloseHandle(host_process_handle);
    return 8;
  }
  CloseHandle(host_process_handle);
  host_process_handle = nullptr;
  CloseHandle(control_read);
  control_read = nullptr;
  CloseHandle(rh[0]);
  rh[0] = nullptr;
  if (rh[1]) {
    CloseHandle(rh[1]);
    rh[1] = nullptr;
  }
  CloseHandle(done_fence_handle);
  done_fence_handle = nullptr;
  if (stereo_done_fence_handle) {
    CloseHandle(stereo_done_fence_handle);
    stereo_done_fence_handle = nullptr;
  }
  if (expected && !deferred_negative) {
    CloseHandle(control_write);
    const DWORD code = wait_process(pi, 10000);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    if (code != expected) {
      std::wcerr << L"negative_mode=" << o.negative << L" expected_exit="
                 << expected << L" actual_exit=" << code << L"\nRESULT FAIL\n";
      return 9;
    }
    std::wcout << L"negative_mode=" << o.negative << L" expected_exit="
               << expected << L" actual_exit=" << code << L"\nRESULT PASS\n";
    return 0;
  }
  HANDLE fh = nullptr;
  HRESULT open = E_FAIL;
  for (int n = 0; n < 5000; ++n) {
    open =
        dev->OpenSharedHandleByName(ready_fence_name.c_str(), GENERIC_ALL, &fh);
    if (SUCCEEDED(open))
      break;
    Sleep(1);
  }
  if (!check(open, "OpenSharedHandleByName(ready-fence)"))
    return 10;
  ComPtr<ID3D12Fence> ready_fence;
  if (!check(dev->OpenSharedHandle(fh, IID_PPV_ARGS(&ready_fence)),
             "OpenSharedHandle(ready-fence)")) {
    CloseHandle(fh);
    return 11;
  }
  CloseHandle(fh);
  ComPtr<ID3D12Fence> stereo_ready_fence;
  if (o.stereo) {
    HANDLE stereo_fh = nullptr;
    HRESULT stereo_open = E_FAIL;
    for (int n = 0; n < 5000; ++n) {
      stereo_open = dev->OpenSharedHandleByName(stereo_ready_fence_name.c_str(),
                                                GENERIC_ALL, &stereo_fh);
      if (SUCCEEDED(stereo_open))
        break;
      Sleep(1);
    }
    if (!check(stereo_open, "OpenSharedHandleByName(stereo-ready1)"))
      return 10;
    if (!check(
            dev->OpenSharedHandle(stereo_fh, IID_PPV_ARGS(&stereo_ready_fence)),
            "OpenSharedHandle(stereo-ready1)")) {
      CloseHandle(stereo_fh);
      return 11;
    }
    CloseHandle(stereo_fh);
  }
  if (o.negative == L"host-termination") {
    HANDLE ready_event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (!ready_event)
      return 11;
    if (!check(ready_fence->SetEventOnCompletion(1, ready_event),
               "SetEventOnCompletion(host-termination-ready)")) {
      CloseHandle(ready_event);
      return 11;
    }
    const DWORD ready_wait = WaitForSingleObject(ready_event, 10000);
    CloseHandle(ready_event);
    if (ready_wait != WAIT_OBJECT_0) {
      std::cerr << "host-termination producer-ready wait failed\n";
      return 11;
    }
    std::cout << "negative_mode=host-termination producer_pid="
              << pi.dwProcessId << " host_exit=24\n"
              << std::flush;
    TerminateProcess(GetCurrentProcess(), 24);
    return 24;
  }
  if (o.negative == L"backpressure-host-termination") {
    HANDLE ready_event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (!ready_event)
      return 11;
    if (!check(ready_fence->SetEventOnCompletion(1, ready_event),
               "SetEventOnCompletion(backpressure-host-termination-ready)")) {
      CloseHandle(ready_event);
      return 11;
    }
    const DWORD ready_wait = WaitForSingleObject(ready_event, 10000);
    CloseHandle(ready_event);
    if (ready_wait != WAIT_OBJECT_0) {
      std::cerr << "backpressure-host-termination producer-ready wait failed\n";
      return 11;
    }
    std::cout << "negative_mode=backpressure-host-termination producer_pid="
              << pi.dwProcessId
              << " ring_depth=1 after_ready_value=1 host_exit=26\n"
              << std::flush;
    TerminateProcess(GetCurrentProcess(), 26);
    return 26;
  }
  if (o.backpressure_depth || o.stereo) {
    CloseHandle(control_write);
    control_write = nullptr;
  }
  const char *shader_src =
      o.stereo_history
          ? R"(RWTexture2D<float4> Target:register(u0);RWTexture2D<float4> History:register(u1);cbuffer FrameState:register(b0){uint FirstFrame;}[numthreads(8,8,1)]void main(uint3 id:SV_DispatchThreadID){float4 v=Target[id.xy];float4 outv=FirstFrame!=0?float4(1.0-v.rgb,v.a):History[id.xy];Target[id.xy]=outv;History[id.xy]=outv;})"
      : o.stereo_contamination
          ? R"(RWTexture2D<float4> Target:register(u0);[numthreads(8,8,1)]void main(uint3 id:SV_DispatchThreadID){float4 v=Target[id.xy];Target[id.xy]=float4(1.0-v.rgb,211.0/255.0);})"
          : R"(RWTexture2D<float4> Target:register(u0);[numthreads(8,8,1)]void main(uint3 id:SV_DispatchThreadID){float4 v=Target[id.xy];Target[id.xy]=float4(1.0-v.rgb,v.a);})";
  ComPtr<ID3DBlob> shader, errors;
  if (!check(D3DCompile(shader_src, std::char_traits<char>::length(shader_src),
                        "bridge", nullptr, nullptr, "main", "cs_5_0",
                        D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &shader, &errors),
             "D3DCompile"))
    return 12;
  D3D12_DESCRIPTOR_RANGE range{};
  range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
  range.NumDescriptors = o.stereo_history ? 2U : 1U;
  range.BaseShaderRegister = 0;
  range.OffsetInDescriptorsFromTableStart =
      D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
  D3D12_ROOT_PARAMETER param{};
  param.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
  param.DescriptorTable.NumDescriptorRanges = 1;
  param.DescriptorTable.pDescriptorRanges = &range;
  D3D12_ROOT_PARAMETER history_param{};
  if (o.stereo_history) {
    history_param.ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    history_param.Constants.ShaderRegister = 0;
    history_param.Constants.RegisterSpace = 0;
    history_param.Constants.Num32BitValues = 1;
  }
  D3D12_ROOT_PARAMETER params[2] = {param, history_param};
  D3D12_ROOT_SIGNATURE_DESC rsd{};
  rsd.NumParameters = o.stereo_history ? 2U : 1U;
  rsd.pParameters = params;
  ComPtr<ID3DBlob> rsblob, rserrors;
  if (!check(D3D12SerializeRootSignature(&rsd, D3D_ROOT_SIGNATURE_VERSION_1,
                                         &rsblob, &rserrors),
             "D3D12SerializeRootSignature"))
    return 13;
  ComPtr<ID3D12RootSignature> root;
  if (!check(dev->CreateRootSignature(0, rsblob->GetBufferPointer(),
                                      rsblob->GetBufferSize(),
                                      IID_PPV_ARGS(&root)),
             "CreateRootSignature"))
    return 14;
  D3D12_COMPUTE_PIPELINE_STATE_DESC pd{};
  pd.pRootSignature = root.Get();
  pd.CS = {shader->GetBufferPointer(), shader->GetBufferSize()};
  ComPtr<ID3D12PipelineState> pso;
  if (!check(dev->CreateComputePipelineState(&pd, IID_PPV_ARGS(&pso)),
             "CreateComputePipelineState"))
    return 15;
  D3D12_DESCRIPTOR_HEAP_DESC hd{};
  hd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
  hd.NumDescriptors = o.stereo_history ? 8U : ltr::bridge_probe::kGenerationCount;
  hd.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
  ComPtr<ID3D12DescriptorHeap> dh;
  if (!check(dev->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&dh)),
             "CreateDescriptorHeap"))
    return 16;
  const UINT ds = dev->GetDescriptorHandleIncrementSize(
      D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
  const auto create_uav_at = [&](ID3D12Resource *resource, std::uint32_t index) {
    D3D12_UNORDERED_ACCESS_VIEW_DESC u{};
    u.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    u.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
    auto h = dh->GetCPUDescriptorHandleForHeapStart();
    h.ptr += static_cast<SIZE_T>(index) * ds;
    dev->CreateUnorderedAccessView(resource, nullptr, &u, h);
  };
  ComPtr<ID3D12Resource> stereo_history[2];
  if (o.stereo_history) {
    for (std::uint32_t eye = 0; eye < 2U; ++eye) {
      stereo_history[eye] = texture(dev.Get(), specs[0].width, specs[0].height);
      if (!stereo_history[eye])
        return 16;
    }
    create_uav_at(res[0].Get(), 0);
    create_uav_at(stereo_history[0].Get(), 1);
    create_uav_at(res[1].Get(), 2);
    create_uav_at(stereo_history[1].Get(), 3);
    create_uav_at(res[0].Get(), 4);
    create_uav_at(stereo_history[1].Get(), 5);
    create_uav_at(res[1].Get(), 6);
    create_uav_at(stereo_history[0].Get(), 7);
  } else {
    create_uav_at(res[0].Get(), 0);
    if (o.backpressure_depth == 2U || o.stereo)
      create_uav_at(res[1].Get(), 1);
  }
  const std::uint32_t total = ltr::bridge_probe::kFramesPerGeneration *
                              ltr::bridge_probe::kGenerationCount;
  D3D12_QUERY_HEAP_DESC qh{};
  qh.Count = total * 2U;
  qh.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
  ComPtr<ID3D12QueryHeap> timestamps;
  if (!check(dev->CreateQueryHeap(&qh, IID_PPV_ARGS(&timestamps)),
             "CreateQueryHeap(timestamp)"))
    return 17;
  D3D12_HEAP_PROPERTIES rbheap{};
  rbheap.Type = D3D12_HEAP_TYPE_READBACK;
  D3D12_RESOURCE_DESC rb{};
  rb.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
  rb.Width = static_cast<UINT64>(total) * 2ULL * sizeof(std::uint64_t);
  rb.Height = 1;
  rb.DepthOrArraySize = 1;
  rb.MipLevels = 1;
  rb.SampleDesc.Count = 1;
  rb.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
  ComPtr<ID3D12Resource> readback;
  if (!check(dev->CreateCommittedResource(&rbheap, D3D12_HEAP_FLAG_NONE, &rb,
                                          D3D12_RESOURCE_STATE_COPY_DEST,
                                          nullptr, IID_PPV_ARGS(&readback)),
             "CreateCommittedResource(timestamp-readback)"))
    return 18;
  std::vector<ComPtr<ID3D12CommandAllocator>> alloc(total);
  std::vector<ComPtr<ID3D12GraphicsCommandList>> lists(total);
  std::uint32_t frame = 0;
  if (o.stereo) {
    for (std::uint32_t temporal = 0;
         temporal < ltr::bridge_probe::kFramesPerGeneration; ++temporal) {
      for (std::uint32_t eye = 0; eye < 2U; ++eye, ++frame) {
        if (!check(dev->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
                                               IID_PPV_ARGS(&alloc[frame])),
                   "CreateCommandAllocator(stereo)"))
          return 19;
        if (!check(dev->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
                                          alloc[frame].Get(), pso.Get(),
                                          IID_PPV_ARGS(&lists[frame])),
                   "CreateCommandList(stereo)"))
          return 20;
        auto *l = lists[frame].Get();
        if (o.stereo_history && temporal == 0) {
          D3D12_RESOURCE_BARRIER hb{};
          hb.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
          hb.Transition.pResource = stereo_history[eye].Get();
          hb.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
          hb.Transition.StateBefore = D3D12_RESOURCE_STATE_COMMON;
          hb.Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
          l->ResourceBarrier(1, &hb);
        }
        D3D12_RESOURCE_BARRIER b{};
        b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        b.Transition.pResource = res[eye].Get();
        b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        b.Transition.StateBefore = D3D12_RESOURCE_STATE_COMMON;
        b.Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        l->ResourceBarrier(1, &b);
        ID3D12DescriptorHeap *heaps[] = {dh.Get()};
        l->SetDescriptorHeaps(1, heaps);
        l->SetComputeRootSignature(root.Get());
        l->SetPipelineState(pso.Get());
        auto gh = dh->GetGPUDescriptorHandleForHeapStart();
        const std::uint32_t descriptor_base =
            o.stereo_history
                ? (o.stereo_history_swap && temporal > 0 ? 4U + eye * 2U
                                                         : eye * 2U)
                : eye;
        gh.ptr += static_cast<UINT64>(descriptor_base) * ds;
        l->SetComputeRootDescriptorTable(0, gh);
        if (o.stereo_history)
          l->SetComputeRoot32BitConstant(1, temporal == 0 ? 1U : 0U, 0);
        l->EndQuery(timestamps.Get(), D3D12_QUERY_TYPE_TIMESTAMP, frame * 2U);
        l->Dispatch((specs[0].width + 7U) / 8U, (specs[0].height + 7U) / 8U, 1);
        l->EndQuery(timestamps.Get(), D3D12_QUERY_TYPE_TIMESTAMP,
                    frame * 2U + 1U);
        D3D12_RESOURCE_BARRIER ub[2]{};
        ub[0].Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
        ub[0].UAV.pResource = res[eye].Get();
        UINT ub_count = 1;
        if (o.stereo_history) {
          ub[1].Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
          ub[1].UAV.pResource =
              stereo_history[o.stereo_history_swap && temporal > 0 ? 1U - eye
                                                                    : eye]
                  .Get();
          ub_count = 2;
        }
        l->ResourceBarrier(ub_count, ub);
        std::swap(b.Transition.StateBefore, b.Transition.StateAfter);
        l->ResourceBarrier(1, &b);
        if (!check(l->Close(), "CommandListClose(stereo)"))
          return 21;
        ID3D12Fence *ready =
            eye == 0 ? ready_fence.Get() : stereo_ready_fence.Get();
        ID3D12Fence *done =
            eye == 0 ? done_fence.Get() : stereo_done_fence.Get();
        const std::uint64_t fence_value =
            static_cast<std::uint64_t>(temporal) + 1ULL;
        if (!check(queue->Wait(ready, fence_value),
                   eye == 0 ? "QueueWait(stereo-left-ready)"
                            : "QueueWait(stereo-right-ready)"))
          return 22;
        ID3D12CommandList *exec[] = {l};
        queue->ExecuteCommandLists(1, exec);
        if (!check(queue->Signal(done, fence_value),
                   eye == 0 ? "QueueSignal(stereo-left-done)"
                            : "QueueSignal(stereo-right-done)"))
          return 23;
      }
    }
  } else if (o.backpressure_depth) {
    Sleep(50);
    for (; frame < total; ++frame) {
      const std::uint32_t slot = frame % o.backpressure_depth;
      if (!check(dev->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
                                             IID_PPV_ARGS(&alloc[frame])),
                 "CreateCommandAllocator(backpressure)"))
        return 19;
      if (!check(dev->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
                                        alloc[frame].Get(), pso.Get(),
                                        IID_PPV_ARGS(&lists[frame])),
                 "CreateCommandList(backpressure)"))
        return 20;
      auto *l = lists[frame].Get();
      D3D12_RESOURCE_BARRIER b{};
      b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
      b.Transition.pResource = res[slot].Get();
      b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
      b.Transition.StateBefore = D3D12_RESOURCE_STATE_COMMON;
      b.Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
      l->ResourceBarrier(1, &b);
      ID3D12DescriptorHeap *heaps[] = {dh.Get()};
      l->SetDescriptorHeaps(1, heaps);
      l->SetComputeRootSignature(root.Get());
      l->SetPipelineState(pso.Get());
      auto gh = dh->GetGPUDescriptorHandleForHeapStart();
      gh.ptr += static_cast<UINT64>(slot) * ds;
      l->SetComputeRootDescriptorTable(0, gh);
      l->EndQuery(timestamps.Get(), D3D12_QUERY_TYPE_TIMESTAMP, frame * 2U);
      l->Dispatch((specs[0].width + 7U) / 8U, (specs[0].height + 7U) / 8U, 1);
      l->EndQuery(timestamps.Get(), D3D12_QUERY_TYPE_TIMESTAMP,
                  frame * 2U + 1U);
      D3D12_RESOURCE_BARRIER ub{};
      ub.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
      ub.UAV.pResource = res[slot].Get();
      l->ResourceBarrier(1, &ub);
      std::swap(b.Transition.StateBefore, b.Transition.StateAfter);
      l->ResourceBarrier(1, &b);
      if (!check(l->Close(), "CommandListClose(backpressure)"))
        return 21;
      if (!check(queue->Wait(ready_fence.Get(),
                             static_cast<std::uint64_t>(frame) + 1ULL),
                 "QueueWait(backpressure-ready)"))
        return 22;
      ID3D12CommandList *exec[] = {l};
      queue->ExecuteCommandLists(1, exec);
      if (!check(queue->Signal(done_fence.Get(),
                               static_cast<std::uint64_t>(frame) + 1ULL),
                 "QueueSignal(backpressure-done)"))
        return 23;
    }
  } else {
    for (std::uint32_t g = 0; g < ltr::bridge_probe::kGenerationCount; ++g) {
      if (g == 1) {
        HANDLE transition_event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        if (!transition_event)
          return 19;
        if (done_fence->GetCompletedValue() < frame &&
            (!check(done_fence->SetEventOnCompletion(frame, transition_event),
                    "SetEventOnCompletion(generation0-done)") ||
             WaitForSingleObject(transition_event, 30000) != WAIT_OBJECT_0)) {
          CloseHandle(transition_event);
          std::cerr << "generation transition wait failed\n";
          return 19;
        }
        CloseHandle(transition_event);

        res[0].Reset();
        if (o.negative == L"client-termination") {
          if (!TerminateProcess(pi.hProcess, expected)) {
            std::cerr << "TerminateProcess(client) failed error="
                      << GetLastError() << "\n";
            return 19;
          }
          CloseHandle(control_write);
          control_write = nullptr;
          const DWORD code = wait_process(pi, 10000);
          CloseHandle(pi.hThread);
          CloseHandle(pi.hProcess);
          if (code != expected) {
            std::wcerr << L"negative_mode=" << o.negative << L" expected_exit="
                       << expected << L" actual_exit=" << code
                       << L"\nRESULT FAIL\n";
            return 19;
          }
          std::wcout << L"negative_mode=" << o.negative << L" expected_exit="
                     << expected << L" actual_exit=" << code
                     << L" cleanup=completed\nRESULT PASS\n";
          return 0;
        }
        if (o.negative == L"device-removal") {
          ComPtr<ID3D12Device5> dev5;
          if (!check(dev.As(&dev5), "ID3D12Device5"))
            return 19;
          dev5->RemoveDevice();
          const HRESULT removed = dev->GetDeviceRemovedReason();
          CloseHandle(control_write);
          control_write = nullptr;
          const DWORD code = wait_process(pi, 10000);
          CloseHandle(pi.hThread);
          CloseHandle(pi.hProcess);
          if (SUCCEEDED(removed) || code != expected) {
            std::cerr << "negative_mode=device-removal device_removed_hr=0x"
                      << std::hex << static_cast<unsigned long>(removed)
                      << std::dec << " expected_producer_exit=" << expected
                      << " actual_producer_exit=" << code << "\nRESULT FAIL\n";
            return 19;
          }
          std::cout << "negative_mode=device-removal device_removed_hr=0x"
                    << std::hex << static_cast<unsigned long>(removed)
                    << std::dec << " producer_exit=" << code
                    << " cleanup=completed\nRESULT PASS\n";
          return 0;
        }
        res[g] = texture(dev.Get(), specs[g].width, specs[g].height);
        if (!res[g])
          return 19;
        if (!check(dev->CreateSharedHandle(res[g].Get(), nullptr, GENERIC_ALL,
                                           nullptr, &rh[g]),
                   "CreateSharedHandle(dynamic-resource)"))
          return 19;
        HANDLE remote_handle = nullptr;
        if (!DuplicateHandle(GetCurrentProcess(), rh[g], pi.hProcess,
                             &remote_handle, 0, FALSE, DUPLICATE_SAME_ACCESS)) {
          std::cerr << "DuplicateHandle(dynamic-resource) failed error="
                    << GetLastError() << "\n";
          CloseHandle(rh[g]);
          return 19;
        }
        CloseHandle(rh[g]);
        rh[g] = nullptr;
        ltr::bridge_probe::DynamicResourceMessage message{};
        message.magic = ltr::bridge_probe::kControlMagic;
        message.protocol = ltr::bridge_probe::kProtocolVersion;
        message.generation = deferred_negative ? g + 1U : g;
        message.width = specs[g].width;
        message.height = specs[g].height;
        message.resource_handle = static_cast<std::uint64_t>(
            reinterpret_cast<std::uintptr_t>(remote_handle));
        DWORD bytes = 0;
        if (!WriteFile(control_write, &message, sizeof(message), &bytes,
                       nullptr) ||
            bytes != sizeof(message)) {
          std::cerr << "WriteFile(dynamic-resource) failed bytes=" << bytes
                    << " error=" << GetLastError() << "\n";
          CloseHandle(control_write);
          control_write = nullptr;
          return 19;
        }
        CloseHandle(control_write);
        control_write = nullptr;
        if (deferred_negative) {
          const DWORD code = wait_process(pi, 10000);
          CloseHandle(pi.hThread);
          CloseHandle(pi.hProcess);
          if (code != expected) {
            std::wcerr << L"negative_mode=" << o.negative << L" expected_exit="
                       << expected << L" actual_exit=" << code
                       << L"\nRESULT FAIL\n";
            return 19;
          }
          std::wcout << L"negative_mode=" << o.negative << L" expected_exit="
                     << expected << L" actual_exit=" << code
                     << L"\nRESULT PASS\n";
          return 0;
        }
        create_uav_at(res[g].Get(), g);
        std::cout << "dynamic_resource_sent generation=" << g
                  << " size=" << specs[g].width << "x" << specs[g].height
                  << " after_completed_frame=" << frame << "\n";
      }
      for (std::uint32_t local = 0;
           local < ltr::bridge_probe::kFramesPerGeneration; ++local, ++frame) {
        if (!check(dev->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
                                               IID_PPV_ARGS(&alloc[frame])),
                   "CreateCommandAllocator"))
          return 19;
        if (!check(dev->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
                                          alloc[frame].Get(), pso.Get(),
                                          IID_PPV_ARGS(&lists[frame])),
                   "CreateCommandList"))
          return 20;
        auto *l = lists[frame].Get();
        D3D12_RESOURCE_BARRIER b{};
        b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        b.Transition.pResource = res[g].Get();
        b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        b.Transition.StateBefore = D3D12_RESOURCE_STATE_COMMON;
        b.Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        l->ResourceBarrier(1, &b);
        ID3D12DescriptorHeap *heaps[] = {dh.Get()};
        l->SetDescriptorHeaps(1, heaps);
        l->SetComputeRootSignature(root.Get());
        l->SetPipelineState(pso.Get());
        auto gh = dh->GetGPUDescriptorHandleForHeapStart();
        gh.ptr += static_cast<UINT64>(g) * ds;
        l->SetComputeRootDescriptorTable(0, gh);
        l->EndQuery(timestamps.Get(), D3D12_QUERY_TYPE_TIMESTAMP, frame * 2U);
        l->Dispatch((specs[g].width + 7U) / 8U, (specs[g].height + 7U) / 8U, 1);
        l->EndQuery(timestamps.Get(), D3D12_QUERY_TYPE_TIMESTAMP,
                    frame * 2U + 1U);
        D3D12_RESOURCE_BARRIER ub{};
        ub.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
        ub.UAV.pResource = res[g].Get();
        l->ResourceBarrier(1, &ub);
        std::swap(b.Transition.StateBefore, b.Transition.StateAfter);
        l->ResourceBarrier(1, &b);
        if (!check(l->Close(), "CommandListClose"))
          return 21;
        if (!check(queue->Wait(ready_fence.Get(),
                               static_cast<std::uint64_t>(frame) + 1ULL),
                   "QueueWait(producer-ready)"))
          return 22;
        ID3D12CommandList *exec[] = {l};
        queue->ExecuteCommandLists(1, exec);
        if (!check(queue->Signal(done_fence.Get(),
                                 static_cast<std::uint64_t>(frame) + 1ULL),
                   "QueueSignal(consumer-done)"))
          return 23;
      }
    }
  }
  ComPtr<ID3D12CommandAllocator> ra;
  ComPtr<ID3D12GraphicsCommandList> rl;
  if (!check(dev->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
                                         IID_PPV_ARGS(&ra)),
             "CreateCommandAllocator(resolve)"))
    return 24;
  if (!check(dev->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, ra.Get(),
                                    nullptr, IID_PPV_ARGS(&rl)),
             "CreateCommandList(resolve)"))
    return 25;
  rl->ResolveQueryData(timestamps.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 0,
                       total * 2U, readback.Get(), 0);
  if (!check(rl->Close(), "CommandListClose(resolve)"))
    return 26;
  ID3D12CommandList *rls[] = {rl.Get()};
  queue->ExecuteCommandLists(1, rls);
  ComPtr<ID3D12Fence> resolve_fence;
  if (!check(dev->CreateFence(0, D3D12_FENCE_FLAG_NONE,
                              IID_PPV_ARGS(&resolve_fence)),
             "CreateFence(resolve)"))
    return 27;
  if (!check(queue->Signal(resolve_fence.Get(), 1),
             "QueueSignal(resolve-done)"))
    return 27;
  HANDLE event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
  if (!event)
    return 28;
  if (!check(resolve_fence->SetEventOnCompletion(1, event),
             "SetEventOnCompletion(resolve)")) {
    CloseHandle(event);
    return 29;
  }
  const DWORD rw = WaitForSingleObject(event, 30000);
  CloseHandle(event);
  if (rw != WAIT_OBJECT_0) {
    std::cerr << "timestamp resolve timed out\n";
    return 30;
  }
  const DWORD producer_exit = wait_process(pi, 30000);
  const auto end = std::chrono::steady_clock::now();
  CloseHandle(pi.hThread);
  CloseHandle(pi.hProcess);
  if (o.stereo_contamination) {
    if (producer_exit != 25) {
      std::cerr
          << "negative_mode=stereo-contamination expected_exit=25 actual_exit="
          << producer_exit << "\nRESULT FAIL\n";
      return 31;
    }
    std::cout
        << "negative_mode=stereo-contamination expected_exit=25 actual_exit="
        << producer_exit << " detector=cross_eye_marker\nRESULT PASS\n";
    return 0;
  }
  if (o.stereo_history_swap) {
    if (producer_exit != 25) {
      std::cerr << "negative_mode=stereo-history-swap expected_exit=25 actual_exit="
                << producer_exit << "\nRESULT FAIL\n";
      return 31;
    }
    std::cout << "negative_mode=stereo-history-swap expected_exit=25 actual_exit="
              << producer_exit
              << " detector=per_eye_history_identity\nRESULT PASS\n";
    return 0;
  }
  if (producer_exit != 0) {
    std::cerr << "producer failed exit=" << producer_exit << "\n";
    return 31;
  }
  UINT64 freq = 0;
  if (!check(queue->GetTimestampFrequency(&freq), "GetTimestampFrequency"))
    return 32;
  std::uint64_t *data = nullptr;
  D3D12_RANGE rr{0, static_cast<SIZE_T>(rb.Width)};
  if (!check(readback->Map(0, &rr, reinterpret_cast<void **>(&data)),
             "Map(timestamp-readback)"))
    return 33;
  double sum = 0.0, minv = std::numeric_limits<double>::max(), maxv = 0.0;
  for (std::uint32_t f = 0; f < total; ++f) {
    const double us = static_cast<double>(data[f * 2U + 1U] - data[f * 2U]) *
                      1000000.0 / static_cast<double>(freq);
    sum += us;
    minv = std::min(minv, us);
    maxv = std::max(maxv, us);
  }
  D3D12_RANGE wr{0, 0};
  readback->Unmap(0, &wr);
  const double wall =
      std::chrono::duration<double, std::milli>(end - start).count();
  if (o.backpressure_depth) {
    std::cout << "consumer_bitness=64 mode=backpressure ring_depth="
              << o.backpressure_depth
              << " ownership=host_created_d3d12_resource "
                 "transform=d3d12_compute_invert transport_gpu_copies=0\n"
              << "frames=" << total << " host_delay_ms=50\n"
              << std::fixed << std::setprecision(3)
              << "host_gpu_compute_mean_us=" << (sum / total)
              << " min_us=" << minv << " max_us=" << maxv
              << " process_wall_ms=" << wall << "\nRESULT PASS\n";
    return 0;
  }
  if (o.stereo) {
    std::cout << "consumer_bitness=64 mode="
              << (o.stereo_history ? "stereo-history" : "stereo")
              << " eyes=2 temporal_frames="
              << ltr::bridge_probe::kFramesPerGeneration
              << " view_dispatches=" << total
              << " ownership=host_created_d3d12_resource "
                 "synchronization=independent_per_eye_fences "
              << (o.stereo_history ? "history=independent_per_eye_gpu_uav " : "")
              << "transform=d3d12_compute_invert transport_gpu_copies=0\n"
              << std::fixed << std::setprecision(3)
              << "host_gpu_compute_mean_us=" << (sum / total)
              << " min_us=" << minv << " max_us=" << maxv
              << " process_wall_ms=" << wall << "\nRESULT PASS\n";
    return 0;
  }
  const char *mode_name = "multiframe";
  const char *transfer_kind = "none";
  if (o.renderer_copy_mode == 1U) {
    mode_name = "renderer-copy";
    transfer_kind = "copy";
  } else if (o.renderer_copy_mode == 2U) {
    mode_name = "renderer-resolve";
    transfer_kind = "resolve-msaa4x";
  } else if (o.renderer_copy_mode == 3U) {
    mode_name = "renderer-convert";
    transfer_kind = "shader-r10-to-rgba8";
  } else if (o.renderer_copy_mode == 4U) {
    mode_name = o.renderer_profile == L"d3d9ex-r10-relay-highres"
                    ? "d3d9ex-relay-highres"
                    : "d3d9ex-relay";
    transfer_kind = "d3d9ex-r10-relay-shader";
  }
  std::cout << "consumer_bitness=64 mode=" << mode_name
            << " ownership=host_created_d3d12_resource "
               "transform=d3d12_compute_invert transport_gpu_copies=0\n"
            << "protocol_version=" << ltr::bridge_probe::kProtocolVersion
            << " generations=" << ltr::bridge_probe::kGenerationCount
            << " frames=" << total << " generation_size_transitions="
            << (ltr::bridge_probe::kGenerationCount - 1U)
            << " generation0=" << specs[0].width << "x" << specs[0].height
            << " generation1=" << specs[1].width << "x" << specs[1].height
            << " renderer_transfer_kind=" << transfer_kind
            << " renderer_to_shared_gpu_transfers="
            << (o.renderer_copy_mode ? total : 0U)
            << " validation_gpu_copies=" << total << "\n"
            << std::fixed << std::setprecision(3)
            << "host_gpu_compute_mean_us=" << (sum / total)
            << " min_us=" << minv << " max_us=" << maxv
            << " process_wall_ms=" << wall << "\nRESULT PASS\n";
  return 0;
}
