#include "common.h"

#include <windows.h>
#include <d3d12.h>
#include <d3dcompiler.h>
#include <wrl/client.h>

#include <cstdint>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>

using Microsoft::WRL::ComPtr;

namespace {
[[nodiscard]] bool check(HRESULT hr, const char* what) {
    if (SUCCEEDED(hr)) return true;
    std::cerr << what << " failed hr=0x" << std::hex << static_cast<unsigned long>(hr) << std::dec << "\n";
    return false;
}
[[nodiscard]] std::wstring quote(const std::wstring& value) { return L"\"" + value + L"\""; }
}

int wmain(int argc, wchar_t** argv) {
    static_assert(sizeof(void*) == 8, "consumer must be built x64");
    if (argc != 3 || std::wstring_view(argv[1]) != L"--producer") return 2;
    const std::wstring producer_path = argv[2];

    ComPtr<ID3D12Device> device;
    if (!check(D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device)), "D3D12CreateDevice")) return 3;
    const LUID luid = device->GetAdapterLuid();

    D3D12_COMMAND_QUEUE_DESC qd{};
    qd.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    ComPtr<ID3D12CommandQueue> queue;
    if (!check(device->CreateCommandQueue(&qd, IID_PPV_ARGS(&queue)), "CreateCommandQueue")) return 4;

    D3D12_HEAP_PROPERTIES heap_props{};
    heap_props.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC rd{};
    rd.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    rd.Width = ltr::bridge_probe::kWidth;
    rd.Height = ltr::bridge_probe::kHeight;
    rd.DepthOrArraySize = 1;
    rd.MipLevels = 1;
    rd.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    rd.SampleDesc.Count = 1;
    rd.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    rd.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET |
        D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS |
        D3D12_RESOURCE_FLAG_ALLOW_SIMULTANEOUS_ACCESS;

    ComPtr<ID3D12Resource> shared_texture;
    if (!check(device->CreateCommittedResource(&heap_props, D3D12_HEAP_FLAG_SHARED, &rd,
            D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&shared_texture)),
        "CreateCommittedResource(shared)")) return 5;

    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;
    HANDLE resource_handle = nullptr;
    if (!check(device->CreateSharedHandle(shared_texture.Get(), &sa, GENERIC_ALL, nullptr, &resource_handle),
        "CreateSharedHandle(resource)")) return 7;
    const std::wstring fence_name = L"Local\\LTRBridgeFence_" + std::to_wstring(GetCurrentProcessId());

    std::wostringstream command;
    command << quote(producer_path)
            << L" --resource-handle " << static_cast<unsigned long long>(reinterpret_cast<std::uintptr_t>(resource_handle))
            << L" --fence-name " << quote(fence_name)
            << L" --luid-low " << luid.LowPart
            << L" --luid-high " << luid.HighPart;
    std::wstring command_line = command.str();
    STARTUPINFOW si{}; si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(nullptr, command_line.data(), nullptr, nullptr, TRUE, 0, nullptr, nullptr, &si, &pi)) return 9;
    CloseHandle(resource_handle);

    HANDLE fence_handle = nullptr;
    HRESULT open_fence_result = E_FAIL;
    for (int attempt = 0; attempt < 5000; ++attempt) {
        open_fence_result = device->OpenSharedHandleByName(
            fence_name.c_str(),
            GENERIC_ALL,
            &fence_handle);
        if (SUCCEEDED(open_fence_result)) break;
        Sleep(1);
    }
    if (!check(open_fence_result, "OpenSharedHandleByName(fence)")) return 10;
    ComPtr<ID3D12Fence> fence;
    if (!check(device->OpenSharedHandle(fence_handle, IID_PPV_ARGS(&fence)), "OpenSharedHandle(fence)")) {
        CloseHandle(fence_handle);
        return 10;
    }
    CloseHandle(fence_handle);

    if (!check(queue->Wait(fence.Get(), ltr::bridge_probe::kProducerReadyFence), "QueueWait(producer-ready)")) return 10;

    const char* shader_source = R"(
RWTexture2D<float4> Target : register(u0);
[numthreads(8,8,1)]
void main(uint3 id : SV_DispatchThreadID) {
    float4 v = Target[id.xy];
    Target[id.xy] = float4(1.0 - v.rgb, v.a);
}
)";
    ComPtr<ID3DBlob> shader, errors;
    if (!check(D3DCompile(shader_source, std::char_traits<char>::length(shader_source), "bridge", nullptr, nullptr,
            "main", "cs_5_0", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &shader, &errors), "D3DCompile")) return 11;

    D3D12_DESCRIPTOR_RANGE range{};
    range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
    range.NumDescriptors = 1;
    range.BaseShaderRegister = 0;
    range.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
    D3D12_ROOT_PARAMETER param{};
    param.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    param.DescriptorTable.NumDescriptorRanges = 1;
    param.DescriptorTable.pDescriptorRanges = &range;
    D3D12_ROOT_SIGNATURE_DESC rsd{};
    rsd.NumParameters = 1;
    rsd.pParameters = &param;
    ComPtr<ID3DBlob> rs_blob, rs_errors;
    if (!check(D3D12SerializeRootSignature(&rsd, D3D_ROOT_SIGNATURE_VERSION_1, &rs_blob, &rs_errors),
        "D3D12SerializeRootSignature")) return 12;
    ComPtr<ID3D12RootSignature> root;
    if (!check(device->CreateRootSignature(0, rs_blob->GetBufferPointer(), rs_blob->GetBufferSize(), IID_PPV_ARGS(&root)),
        "CreateRootSignature")) return 13;
    D3D12_COMPUTE_PIPELINE_STATE_DESC psd{};
    psd.pRootSignature = root.Get();
    psd.CS = {shader->GetBufferPointer(), shader->GetBufferSize()};
    ComPtr<ID3D12PipelineState> pso;
    if (!check(device->CreateComputePipelineState(&psd, IID_PPV_ARGS(&pso)), "CreateComputePipelineState")) return 14;

    D3D12_DESCRIPTOR_HEAP_DESC hd{};
    hd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    hd.NumDescriptors = 1;
    hd.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    ComPtr<ID3D12DescriptorHeap> desc_heap;
    if (!check(device->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&desc_heap)), "CreateDescriptorHeap")) return 15;
    D3D12_UNORDERED_ACCESS_VIEW_DESC uav{};
    uav.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    uav.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
    device->CreateUnorderedAccessView(shared_texture.Get(), nullptr, &uav, desc_heap->GetCPUDescriptorHandleForHeapStart());

    ComPtr<ID3D12CommandAllocator> alloc;
    if (!check(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&alloc)), "CreateCommandAllocator")) return 16;
    ComPtr<ID3D12GraphicsCommandList> list;
    if (!check(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, alloc.Get(), pso.Get(), IID_PPV_ARGS(&list)),
        "CreateCommandList")) return 17;
    D3D12_RESOURCE_BARRIER b{};
    b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b.Transition.pResource = shared_texture.Get();
    b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    b.Transition.StateBefore = D3D12_RESOURCE_STATE_COMMON;
    b.Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    list->ResourceBarrier(1, &b);
    ID3D12DescriptorHeap* heaps[] = {desc_heap.Get()};
    list->SetDescriptorHeaps(1, heaps);
    list->SetComputeRootSignature(root.Get());
    list->SetPipelineState(pso.Get());
    list->SetComputeRootDescriptorTable(0, desc_heap->GetGPUDescriptorHandleForHeapStart());
    list->Dispatch(ltr::bridge_probe::kWidth / 8U, ltr::bridge_probe::kHeight / 8U, 1);
    D3D12_RESOURCE_BARRIER uav_barrier{};
    uav_barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    uav_barrier.UAV.pResource = shared_texture.Get();
    list->ResourceBarrier(1, &uav_barrier);
    std::swap(b.Transition.StateBefore, b.Transition.StateAfter);
    list->ResourceBarrier(1, &b);
    if (!check(list->Close(), "CommandListClose")) return 18;
    ID3D12CommandList* lists[] = {list.Get()};
    queue->ExecuteCommandLists(1, lists);
    if (!check(queue->Signal(fence.Get(), ltr::bridge_probe::kConsumerDoneFence), "QueueSignal(consumer-done)")) return 19;

    const DWORD wait_result = WaitForSingleObject(pi.hProcess, 30000);
    if (wait_result != WAIT_OBJECT_0) return 20;
    DWORD producer_exit = 0;
    GetExitCodeProcess(pi.hProcess, &producer_exit);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    if (producer_exit != 0) return 21;

    std::cout << "consumer_bitness=64 ownership=host_created_d3d12_resource transform=d3d12_compute_invert gpu_copies=0 pixels="
              << (ltr::bridge_probe::kWidth * ltr::bridge_probe::kHeight) << "\nRESULT PASS\n";
    return 0;
}
