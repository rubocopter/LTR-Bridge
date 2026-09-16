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
};
[[nodiscard]] bool parse(int argc, wchar_t **argv, Options &o) {
  for (int i = 1; i < argc; ++i) {
    const std::wstring_view k(argv[i]);
    if (k == L"--producer" && i + 1 < argc)
      o.producer = argv[++i];
    else if (k == L"--negative" && i + 1 < argc)
      o.negative = argv[++i];
    else
      return false;
  }
  return !o.producer.empty();
}
[[nodiscard]] ComPtr<ID3D12Resource> texture(ID3D12Device *d, std::uint32_t w,
                                             std::uint32_t h) {
  D3D12_HEAP_PROPERTIES hp{};
  hp.Type = D3D12_HEAP_TYPE_DEFAULT;
  D3D12_RESOURCE_DESC r{};
  r.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
  r.Width = w;
  r.Height = h;
  r.DepthOrArraySize = 1;
  r.MipLevels = 1;
  r.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
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
    std::cerr << "usage: consumer --producer <x86-producer> [--negative "
                 "protocol|adapter|resource-contract]\n";
    return 2;
  }
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
  ComPtr<ID3D12Resource> res[ltr::bridge_probe::kGenerationCount];
  HANDLE rh[ltr::bridge_probe::kGenerationCount]{};
  SECURITY_ATTRIBUTES sa{};
  sa.nLength = sizeof(sa);
  sa.bInheritHandle = TRUE;
  for (std::uint32_t g = 0; g < ltr::bridge_probe::kGenerationCount; ++g) {
    res[g] = texture(dev.Get(), ltr::bridge_probe::kGenerations[g].width,
                     ltr::bridge_probe::kGenerations[g].height);
    if (!res[g])
      return 5;
    if (!check(dev->CreateSharedHandle(res[g].Get(), &sa, GENERIC_ALL, nullptr,
                                       &rh[g]),
               "CreateSharedHandle(resource)"))
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
  std::uint32_t protocol = ltr::bridge_probe::kProtocolVersion;
  auto specs = std::vector<ltr::bridge_probe::GenerationSpec>(
      std::begin(ltr::bridge_probe::kGenerations),
      std::end(ltr::bridge_probe::kGenerations));
  DWORD expected = 0;
  std::uint32_t expect_host_stall = 0;
  if (o.negative == L"protocol") {
    ++protocol;
    expected = 3;
  } else if (o.negative == L"adapter") {
    luid.LowPart ^= 0x7FFFFFFFU;
    expected = 5;
  } else if (o.negative == L"resource-contract") {
    ++specs[1].width;
    expected = 9;
  } else if (o.negative == L"host-stall") {
    expect_host_stall = 1;
    expected = 17;
  } else if (!o.negative.empty()) {
    std::cerr << "unknown negative mode\n";
    return 7;
  }
  const std::wstring ready_fence_name =
      L"Local\\LTRBridgeReadyFence_" + std::to_wstring(GetCurrentProcessId());
  std::wostringstream cmd;
  cmd << quote(o.producer) << L" --resource0-handle "
      << static_cast<unsigned long long>(
             reinterpret_cast<std::uintptr_t>(rh[0]))
      << L" --resource1-handle "
      << static_cast<unsigned long long>(
             reinterpret_cast<std::uintptr_t>(rh[1]))
      << L" --done-fence-handle "
      << static_cast<unsigned long long>(
             reinterpret_cast<std::uintptr_t>(done_fence_handle))
      << L" --width0 " << specs[0].width << L" --height0 " << specs[0].height
      << L" --width1 " << specs[1].width << L" --height1 " << specs[1].height
      << L" --ready-fence-name " << quote(ready_fence_name) << L" --luid-low "
      << luid.LowPart << L" --luid-high " << luid.HighPart
      << L" --protocol-version " << protocol << L" --frames-per-generation "
      << ltr::bridge_probe::kFramesPerGeneration << L" --expect-host-stall "
      << expect_host_stall;
  std::wstring line = cmd.str();
  STARTUPINFOW si{};
  si.cb = sizeof(si);
  PROCESS_INFORMATION pi{};
  const auto start = std::chrono::steady_clock::now();
  if (!CreateProcessW(nullptr, line.data(), nullptr, nullptr, TRUE, 0, nullptr,
                      nullptr, &si, &pi)) {
    std::cerr << "CreateProcessW failed error=" << GetLastError() << "\n";
    return 8;
  }
  for (auto h : rh)
    CloseHandle(h);
  CloseHandle(done_fence_handle);
  if (expected) {
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
  const char *shader_src =
      R"(RWTexture2D<float4> Target:register(u0);[numthreads(8,8,1)]void main(uint3 id:SV_DispatchThreadID){float4 v=Target[id.xy];Target[id.xy]=float4(1.0-v.rgb,v.a);})";
  ComPtr<ID3DBlob> shader, errors;
  if (!check(D3DCompile(shader_src, std::char_traits<char>::length(shader_src),
                        "bridge", nullptr, nullptr, "main", "cs_5_0",
                        D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &shader, &errors),
             "D3DCompile"))
    return 12;
  D3D12_DESCRIPTOR_RANGE range{};
  range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
  range.NumDescriptors = 1;
  range.BaseShaderRegister = 0;
  range.OffsetInDescriptorsFromTableStart =
      D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
  D3D12_ROOT_PARAMETER param{};
  param.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
  param.DescriptorTable.NumDescriptorRanges = 1;
  param.DescriptorTable.pDescriptorRanges = &range;
  D3D12_ROOT_SIGNATURE_DESC rsd{};
  rsd.NumParameters = 1;
  rsd.pParameters = &param;
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
  hd.NumDescriptors = ltr::bridge_probe::kGenerationCount;
  hd.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
  ComPtr<ID3D12DescriptorHeap> dh;
  if (!check(dev->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&dh)),
             "CreateDescriptorHeap"))
    return 16;
  const UINT ds = dev->GetDescriptorHandleIncrementSize(
      D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
  for (std::uint32_t g = 0; g < ltr::bridge_probe::kGenerationCount; ++g) {
    D3D12_UNORDERED_ACCESS_VIEW_DESC u{};
    u.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    u.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
    auto h = dh->GetCPUDescriptorHandleForHeapStart();
    h.ptr += static_cast<SIZE_T>(g) * ds;
    dev->CreateUnorderedAccessView(res[g].Get(), nullptr, &u, h);
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
  for (std::uint32_t g = 0; g < ltr::bridge_probe::kGenerationCount; ++g) {
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
      l->Dispatch((ltr::bridge_probe::kGenerations[g].width + 7U) / 8U,
                  (ltr::bridge_probe::kGenerations[g].height + 7U) / 8U, 1);
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
  std::cout << "consumer_bitness=64 ownership=host_created_d3d12_resource "
               "transform=d3d12_compute_invert transport_gpu_copies=0\n"
            << "protocol_version=" << ltr::bridge_probe::kProtocolVersion
            << " generations=" << ltr::bridge_probe::kGenerationCount
            << " frames=" << total << " generation_size_transitions="
            << (ltr::bridge_probe::kGenerationCount - 1U)
            << " validation_gpu_copies=" << total << "\n"
            << std::fixed << std::setprecision(3)
            << "host_gpu_compute_mean_us=" << (sum / total)
            << " min_us=" << minv << " max_us=" << maxv
            << " process_wall_ms=" << wall << "\nRESULT PASS\n";
  return 0;
}
