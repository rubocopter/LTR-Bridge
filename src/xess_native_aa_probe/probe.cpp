#include <d3d12.h>
#include <DirectXPackedVector.h>
#include <dxgi1_6.h>
#include <windows.h>
#include <wrl/client.h>

#include <xess/xess.h>
#include <xess/xess_d3d12.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "../temporal/temporal_sequence.h"

using Microsoft::WRL::ComPtr;

namespace {

#ifndef LTR_XESS_PROBE_WIDTH
#define LTR_XESS_PROBE_WIDTH 256
#endif
#ifndef LTR_XESS_PROBE_HEIGHT
#define LTR_XESS_PROBE_HEIGHT 144
#endif
#ifndef LTR_XESS_PROBE_FRAMES
#define LTR_XESS_PROBE_FRAMES 12
#endif

constexpr uint32_t kWidth = LTR_XESS_PROBE_WIDTH;
constexpr uint32_t kHeight = LTR_XESS_PROBE_HEIGHT;
constexpr uint32_t kFrames = LTR_XESS_PROBE_FRAMES;
constexpr uint32_t kTimingWarmupFrames = 2;
constexpr float kObjectPixelsPerFrame = 3.0f;

static_assert(kWidth > 0 && kHeight > 0, "XeSS probe dimensions must be positive");
static_assert(kFrames > kTimingWarmupFrames, "XeSS probe needs samples after timing warm-up");

void CheckHr(HRESULT hr, const char* what)
{
    if (FAILED(hr)) {
        throw std::runtime_error(std::string(what) + " failed, HRESULT=" + std::to_string(static_cast<uint32_t>(hr)));
    }
}

void CheckXess(xess_result_t result, const char* what)
{
    if (result != XESS_RESULT_SUCCESS) {
        throw std::runtime_error(std::string(what) + " failed, XeSS result=" + std::to_string(static_cast<int>(result)));
    }
}

D3D12_RESOURCE_DESC BufferDesc(uint64_t bytes)
{
    D3D12_RESOURCE_DESC desc{};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    desc.Width = bytes;
    desc.Height = 1;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.SampleDesc.Count = 1;
    desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    return desc;
}

D3D12_HEAP_PROPERTIES HeapProps(D3D12_HEAP_TYPE type)
{
    D3D12_HEAP_PROPERTIES props{};
    props.Type = type;
    props.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
    props.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
    props.CreationNodeMask = 1;
    props.VisibleNodeMask = 1;
    return props;
}

D3D12_RESOURCE_BARRIER Transition(ID3D12Resource* resource, D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after)
{
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = resource;
    barrier.Transition.StateBefore = before;
    barrier.Transition.StateAfter = after;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    return barrier;
}

struct GpuContext {
    ComPtr<IDXGIAdapter1> adapter;
    DXGI_ADAPTER_DESC1 adapterDesc{};
    ComPtr<ID3D12Device> device;
    ComPtr<ID3D12CommandQueue> queue;
    ComPtr<ID3D12CommandAllocator> allocator;
    ComPtr<ID3D12GraphicsCommandList> list;
    ComPtr<ID3D12Fence> fence;
    uint64_t fenceValue = 0;
    HANDLE fenceEvent = nullptr;

    GpuContext() = default;
    GpuContext(const GpuContext&) = delete;
    GpuContext& operator=(const GpuContext&) = delete;
    GpuContext(GpuContext&& other) noexcept
        : adapter(std::move(other.adapter)),
          adapterDesc(other.adapterDesc),
          device(std::move(other.device)),
          queue(std::move(other.queue)),
          allocator(std::move(other.allocator)),
          list(std::move(other.list)),
          fence(std::move(other.fence)),
          fenceValue(other.fenceValue),
          fenceEvent(other.fenceEvent)
    {
        other.fenceEvent = nullptr;
        other.fenceValue = 0;
    }
    GpuContext& operator=(GpuContext&&) = delete;

    ~GpuContext()
    {
        if (fenceEvent) {
            CloseHandle(fenceEvent);
        }
    }

    void Begin()
    {
        CheckHr(allocator->Reset(), "ID3D12CommandAllocator::Reset");
        CheckHr(list->Reset(allocator.Get(), nullptr), "ID3D12GraphicsCommandList::Reset");
    }

    void SubmitAndWait()
    {
        CheckHr(list->Close(), "ID3D12GraphicsCommandList::Close");
        ID3D12CommandList* commandLists[] = {list.Get()};
        queue->ExecuteCommandLists(1, commandLists);
        ++fenceValue;
        CheckHr(queue->Signal(fence.Get(), fenceValue), "ID3D12CommandQueue::Signal");
        if (fence->GetCompletedValue() < fenceValue) {
            CheckHr(fence->SetEventOnCompletion(fenceValue, fenceEvent), "ID3D12Fence::SetEventOnCompletion");
            WaitForSingleObject(fenceEvent, INFINITE);
        }
    }
};

GpuContext CreateGpu()
{
    GpuContext gpu;
    ComPtr<IDXGIFactory6> factory;
    CheckHr(CreateDXGIFactory2(0, IID_PPV_ARGS(&factory)), "CreateDXGIFactory2");

    for (uint32_t index = 0;; ++index) {
        ComPtr<IDXGIAdapter1> candidate;
        const HRESULT enumResult = factory->EnumAdapterByGpuPreference(
            index, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, IID_PPV_ARGS(&candidate));
        if (enumResult == DXGI_ERROR_NOT_FOUND) {
            break;
        }
        CheckHr(enumResult, "EnumAdapterByGpuPreference");

        DXGI_ADAPTER_DESC1 desc{};
        CheckHr(candidate->GetDesc1(&desc), "IDXGIAdapter1::GetDesc1");
        if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) {
            continue;
        }

        ComPtr<ID3D12Device> device;
        if (SUCCEEDED(D3D12CreateDevice(candidate.Get(), D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(&device)))) {
            gpu.adapter = candidate;
            gpu.adapterDesc = desc;
            gpu.device = device;
            break;
        }
    }

    if (!gpu.device) {
        throw std::runtime_error("No hardware D3D12 adapter with feature level 12_0 found");
    }

    D3D12_COMMAND_QUEUE_DESC queueDesc{};
    queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    CheckHr(gpu.device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&gpu.queue)), "CreateCommandQueue");
    CheckHr(gpu.device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&gpu.allocator)), "CreateCommandAllocator");
    CheckHr(gpu.device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, gpu.allocator.Get(), nullptr, IID_PPV_ARGS(&gpu.list)), "CreateCommandList");
    CheckHr(gpu.list->Close(), "Initial command-list close");
    CheckHr(gpu.device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&gpu.fence)), "CreateFence");
    gpu.fenceEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (!gpu.fenceEvent) {
        CheckHr(HRESULT_FROM_WIN32(GetLastError()), "CreateEventW");
    }
    return gpu;
}

ComPtr<ID3D12Resource> CreateTexture(
    ID3D12Device* device,
    DXGI_FORMAT format,
    D3D12_RESOURCE_FLAGS flags,
    D3D12_RESOURCE_STATES initialState)
{
    D3D12_RESOURCE_DESC desc{};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width = kWidth;
    desc.Height = kHeight;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.Format = format;
    desc.SampleDesc.Count = 1;
    desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    desc.Flags = flags;

    auto heap = HeapProps(D3D12_HEAP_TYPE_DEFAULT);
    ComPtr<ID3D12Resource> resource;
    CheckHr(device->CreateCommittedResource(
        &heap,
        D3D12_HEAP_FLAG_NONE,
        &desc,
        initialState,
        nullptr,
        IID_PPV_ARGS(&resource)),
        "CreateCommittedResource(texture)");
    return resource;
}

ComPtr<ID3D12Resource> CreateBuffer(ID3D12Device* device, uint64_t bytes, D3D12_HEAP_TYPE heapType, D3D12_RESOURCE_STATES state)
{
    auto heap = HeapProps(heapType);
    auto desc = BufferDesc(bytes);
    ComPtr<ID3D12Resource> resource;
    CheckHr(device->CreateCommittedResource(
        &heap,
        D3D12_HEAP_FLAG_NONE,
        &desc,
        state,
        nullptr,
        IID_PPV_ARGS(&resource)),
        "CreateCommittedResource(buffer)");
    return resource;
}

void UploadTexture(
    GpuContext& gpu,
    ID3D12Resource* texture,
    const uint8_t* source,
    size_t sourceRowBytes,
    D3D12_RESOURCE_STATES beforeState,
    D3D12_RESOURCE_STATES afterState)
{
    const auto desc = texture->GetDesc();
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
    UINT numRows = 0;
    UINT64 rowBytes = 0;
    UINT64 totalBytes = 0;
    gpu.device->GetCopyableFootprints(&desc, 0, 1, 0, &footprint, &numRows, &rowBytes, &totalBytes);
    if (sourceRowBytes > rowBytes || numRows != kHeight) {
        throw std::runtime_error("Unexpected upload footprint");
    }

    auto upload = CreateBuffer(gpu.device.Get(), totalBytes, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ);
    uint8_t* mapped = nullptr;
    D3D12_RANGE noRead{0, 0};
    CheckHr(upload->Map(0, &noRead, reinterpret_cast<void**>(&mapped)), "Upload buffer map");
    for (uint32_t y = 0; y < kHeight; ++y) {
        std::memcpy(mapped + footprint.Offset + static_cast<size_t>(y) * footprint.Footprint.RowPitch,
            source + static_cast<size_t>(y) * sourceRowBytes,
            sourceRowBytes);
    }
    upload->Unmap(0, nullptr);

    gpu.Begin();
    const auto toCopy = Transition(texture, beforeState, D3D12_RESOURCE_STATE_COPY_DEST);
    gpu.list->ResourceBarrier(1, &toCopy);

    D3D12_TEXTURE_COPY_LOCATION dst{};
    dst.pResource = texture;
    dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    dst.SubresourceIndex = 0;

    D3D12_TEXTURE_COPY_LOCATION src{};
    src.pResource = upload.Get();
    src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    src.PlacedFootprint = footprint;
    gpu.list->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);

    const auto toFinal = Transition(texture, D3D12_RESOURCE_STATE_COPY_DEST, afterState);
    gpu.list->ResourceBarrier(1, &toFinal);
    gpu.SubmitAndWait();
}

std::vector<uint8_t> ReadbackRgba8(GpuContext& gpu, ID3D12Resource* texture)
{
    const auto desc = texture->GetDesc();
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
    UINT numRows = 0;
    UINT64 rowBytes = 0;
    UINT64 totalBytes = 0;
    gpu.device->GetCopyableFootprints(&desc, 0, 1, 0, &footprint, &numRows, &rowBytes, &totalBytes);
    if (rowBytes != kWidth * 4ull || numRows != kHeight) {
        throw std::runtime_error("Unexpected RGBA8 readback footprint");
    }

    auto readback = CreateBuffer(gpu.device.Get(), totalBytes, D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_STATE_COPY_DEST);
    gpu.Begin();
    const auto toCopy = Transition(texture, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
    gpu.list->ResourceBarrier(1, &toCopy);

    D3D12_TEXTURE_COPY_LOCATION src{};
    src.pResource = texture;
    src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    src.SubresourceIndex = 0;
    D3D12_TEXTURE_COPY_LOCATION dst{};
    dst.pResource = readback.Get();
    dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    dst.PlacedFootprint = footprint;
    gpu.list->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);

    const auto toUav = Transition(texture, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    gpu.list->ResourceBarrier(1, &toUav);
    gpu.SubmitAndWait();

    std::vector<uint8_t> result(static_cast<size_t>(kWidth) * kHeight * 4);
    uint8_t* mapped = nullptr;
    D3D12_RANGE readRange{static_cast<SIZE_T>(footprint.Offset), static_cast<SIZE_T>(footprint.Offset + totalBytes)};
    CheckHr(readback->Map(0, &readRange, reinterpret_cast<void**>(&mapped)), "Readback buffer map");
    for (uint32_t y = 0; y < kHeight; ++y) {
        std::memcpy(result.data() + static_cast<size_t>(y) * kWidth * 4,
            mapped + footprint.Offset + static_cast<size_t>(y) * footprint.Footprint.RowPitch,
            static_cast<size_t>(kWidth) * 4);
    }
    D3D12_RANGE noWrite{0, 0};
    readback->Unmap(0, &noWrite);
    return result;
}

struct SyntheticFrame {
    std::vector<uint8_t> color;
    std::vector<uint16_t> velocity;
    std::vector<uint8_t> responsiveMask;
    std::vector<uint8_t> changedMask;
    ltr::temporal::JitterSample jitter{};
    uint32_t movingPixels = 0;
    uint32_t motionPixels = 0;
};

bool InHud(uint32_t x, uint32_t y, uint32_t frame)
{
    const int left = 28 + static_cast<int>(frame) * 6;
    const int top = 42;
    return static_cast<int>(x) >= left && static_cast<int>(x) < left + 46 &&
           static_cast<int>(y) >= top && static_cast<int>(y) < top + 26;
}

SyntheticFrame MakeFrame(uint32_t frame)
{
    SyntheticFrame result;
    result.color.resize(static_cast<size_t>(kWidth) * kHeight * 4);
    result.velocity.resize(static_cast<size_t>(kWidth) * kHeight * 2);
    result.responsiveMask.resize(static_cast<size_t>(kWidth) * kHeight);
    result.changedMask.resize(static_cast<size_t>(kWidth) * kHeight);
    result.jitter = ltr::temporal::JitterForFrame(frame);

    const float objectCenterX = 84.0f + static_cast<float>(frame) * kObjectPixelsPerFrame;
    constexpr float objectCenterY = 93.0f;
    constexpr float objectHalfWidth = 23.0f;
    constexpr float objectHalfHeight = 16.0f;
    const auto objectMotionX = DirectX::PackedVector::XMConvertFloatToHalf(frame == 0 ? 0.0f : -kObjectPixelsPerFrame);
    const auto zeroHalf = DirectX::PackedVector::XMConvertFloatToHalf(0.0f);

    for (uint32_t y = 0; y < kHeight; ++y) {
        for (uint32_t x = 0; x < kWidth; ++x) {
            const size_t pixel = static_cast<size_t>(y) * kWidth + x;
            const bool hudNow = InHud(x, y, frame);
            const bool hudPrevious = frame > 0 && InHud(x, y, frame - 1);
            const bool changed = hudNow != hudPrevious;

            // Mirror the harness convention: positive jitter shifts projected scene content right/down,
            // while motion remains current -> previous in render pixels with jitter excluded.
            const float sceneX = static_cast<float>(x) + 0.5f - result.jitter.x_pixels;
            const float sceneY = static_cast<float>(y) + 0.5f - result.jitter.y_pixels;
            const bool movingObject =
                std::abs(sceneX - objectCenterX) <= objectHalfWidth &&
                std::abs(sceneY - objectCenterY) <= objectHalfHeight;
            const bool dilatedObjectMotion =
                std::abs(sceneX - objectCenterX) <= objectHalfWidth + 1.0f &&
                std::abs(sceneY - objectCenterY) <= objectHalfHeight + 1.0f;

            const float wave = 61.0f + 18.0f * std::sin(sceneX * 0.11f) + 14.0f * std::cos(sceneY * 0.15f);
            const uint8_t background = static_cast<uint8_t>(std::clamp(wave, 20.0f, 120.0f));
            const uint8_t r = hudNow ? 245 : (movingObject ? 212 : background);
            const uint8_t g = hudNow ? 245 : (movingObject ? 112 : static_cast<uint8_t>(std::min(255, background + 9)));
            const uint8_t b = hudNow ? 245 : (movingObject ? 58 : static_cast<uint8_t>(std::min(255, background + 18)));
            const size_t out = pixel * 4;
            result.color[out + 0] = r;
            result.color[out + 1] = g;
            result.color[out + 2] = b;
            result.color[out + 3] = 255;
            result.velocity[pixel * 2 + 0] = dilatedObjectMotion ? objectMotionX : zeroHalf;
            result.velocity[pixel * 2 + 1] = zeroHalf;
            if (movingObject) {
                ++result.movingPixels;
            }
            if (dilatedObjectMotion && frame != 0) {
                ++result.motionPixels;
            }
            result.responsiveMask[pixel] = changed ? 255 : 0;
            result.changedMask[pixel] = changed ? 1 : 0;
        }
    }
    return result;
}

struct XessContext {
    xess_context_handle_t handle = nullptr;
    xess_properties_t properties{};
    ComPtr<ID3D12Resource> output;

    XessContext() = default;
    XessContext(const XessContext&) = delete;
    XessContext& operator=(const XessContext&) = delete;
    XessContext(XessContext&& other) noexcept
        : handle(other.handle), properties(other.properties), output(std::move(other.output))
    {
        other.handle = nullptr;
    }
    XessContext& operator=(XessContext&&) = delete;

    ~XessContext()
    {
        if (handle) {
            xessDestroyContext(handle);
        }
    }
};

XessContext CreateXessContext(GpuContext& gpu, bool responsiveMask)
{
    XessContext context;
    CheckXess(xessD3D12CreateContext(gpu.device.Get(), &context.handle), "xessD3D12CreateContext");

    xess_2d_t outputResolution{kWidth, kHeight};
    xess_2d_t inputResolution{};
    CheckXess(xessGetInputResolution(context.handle, &outputResolution, XESS_QUALITY_SETTING_AA, &inputResolution), "xessGetInputResolution");
    if (inputResolution.x != kWidth || inputResolution.y != kHeight) {
        throw std::runtime_error("XeSS Native AA did not report 1.0x input resolution");
    }
    CheckXess(xessGetProperties(context.handle, &outputResolution, &context.properties), "xessGetProperties");

    xess_d3d12_init_params_t init{};
    init.outputResolution = outputResolution;
    init.qualitySetting = XESS_QUALITY_SETTING_AA;
    init.initFlags = XESS_INIT_FLAG_HIGH_RES_MV | XESS_INIT_FLAG_LDR_INPUT_COLOR;
    if (responsiveMask) {
        init.initFlags |= XESS_INIT_FLAG_RESPONSIVE_PIXEL_MASK;
    }
    CheckXess(xessD3D12Init(context.handle, &init), "xessD3D12Init");

    context.output = CreateTexture(
        gpu.device.Get(),
        DXGI_FORMAT_R8G8B8A8_UNORM,
        D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    return context;
}

double ExecuteXess(
    GpuContext& gpu,
    XessContext& context,
    ID3D12Resource* color,
    ID3D12Resource* velocity,
    ID3D12Resource* responsiveMask,
    bool resetHistory,
    const ltr::temporal::JitterSample& jitter)
{
    D3D12_QUERY_HEAP_DESC queryDesc{};
    queryDesc.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
    queryDesc.Count = 2;
    ComPtr<ID3D12QueryHeap> queryHeap;
    CheckHr(gpu.device->CreateQueryHeap(&queryDesc, IID_PPV_ARGS(&queryHeap)), "CreateQueryHeap(timestamp)");
    auto timingReadback = CreateBuffer(
        gpu.device.Get(), sizeof(uint64_t) * 2, D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_STATE_COPY_DEST);
    uint64_t timestampFrequency = 0;
    CheckHr(gpu.queue->GetTimestampFrequency(&timestampFrequency), "GetTimestampFrequency");
    if (timestampFrequency == 0) {
        throw std::runtime_error("D3D12 timestamp frequency is zero");
    }

    gpu.Begin();
    xess_d3d12_execute_params_t exec{};
    exec.pColorTexture = color;
    exec.pVelocityTexture = velocity;
    exec.pResponsivePixelMaskTexture = responsiveMask;
    exec.pOutputTexture = context.output.Get();
    exec.jitterOffsetX = jitter.x_pixels;
    exec.jitterOffsetY = jitter.y_pixels;
    exec.exposureScale = 1.0f;
    exec.resetHistory = resetHistory ? 1u : 0u;
    exec.inputWidth = kWidth;
    exec.inputHeight = kHeight;
    gpu.list->EndQuery(queryHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 0);
    CheckXess(xessD3D12Execute(context.handle, gpu.list.Get(), &exec), "xessD3D12Execute");
    gpu.list->EndQuery(queryHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 1);
    gpu.list->ResolveQueryData(queryHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 0, 2, timingReadback.Get(), 0);
    gpu.SubmitAndWait();

    uint64_t* timestamps = nullptr;
    D3D12_RANGE readRange{0, sizeof(uint64_t) * 2};
    CheckHr(timingReadback->Map(0, &readRange, reinterpret_cast<void**>(&timestamps)), "Timestamp readback map");
    const uint64_t start = timestamps[0];
    const uint64_t end = timestamps[1];
    D3D12_RANGE noWrite{0, 0};
    timingReadback->Unmap(0, &noWrite);
    if (end <= start) {
        throw std::runtime_error("Invalid XeSS GPU timestamp interval");
    }
    return static_cast<double>(end - start) * 1000.0 / static_cast<double>(timestampFrequency);
}

struct TimingStats {
    double meanMs = 0.0;
    double minMs = 0.0;
    double maxMs = 0.0;
};

TimingStats SummarizeTimings(const std::vector<double>& timings, size_t skip)
{
    if (timings.size() <= skip) {
        throw std::runtime_error("Not enough GPU timing samples");
    }
    TimingStats stats{};
    stats.minMs = timings[skip];
    stats.maxMs = timings[skip];
    double sum = 0.0;
    for (size_t i = skip; i < timings.size(); ++i) {
        sum += timings[i];
        stats.minMs = std::min(stats.minMs, timings[i]);
        stats.maxMs = std::max(stats.maxMs, timings[i]);
    }
    stats.meanMs = sum / static_cast<double>(timings.size() - skip);
    return stats;
}

double MeanAbsoluteRgbError(
    const std::vector<uint8_t>& reconstructed,
    const std::vector<uint8_t>& reference,
    const std::vector<uint8_t>& mask)
{
    uint64_t sum = 0;
    uint64_t samples = 0;
    for (size_t pixel = 0; pixel < mask.size(); ++pixel) {
        if (!mask[pixel]) {
            continue;
        }
        for (size_t channel = 0; channel < 3; ++channel) {
            const int a = reconstructed[pixel * 4 + channel];
            const int b = reference[pixel * 4 + channel];
            sum += static_cast<uint64_t>(std::abs(a - b));
            ++samples;
        }
    }
    return samples ? static_cast<double>(sum) / static_cast<double>(samples) : 0.0;
}

uint64_t HashBytes(const std::vector<uint8_t>& bytes)
{
    uint64_t hash = 1469598103934665603ull;
    for (const uint8_t value : bytes) {
        hash ^= value;
        hash *= 1099511628211ull;
    }
    return hash;
}

} // namespace

int main()
{
    try {
        auto gpu = CreateGpu();

        auto color = CreateTexture(gpu.device.Get(), DXGI_FORMAT_R8G8B8A8_UNORM, D3D12_RESOURCE_FLAG_NONE,
            D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        auto velocity = CreateTexture(gpu.device.Get(), DXGI_FORMAT_R16G16_FLOAT, D3D12_RESOURCE_FLAG_NONE,
            D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        auto responsiveMask = CreateTexture(gpu.device.Get(), DXGI_FORMAT_R8_UNORM, D3D12_RESOURCE_FLAG_NONE,
            D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

        auto baseline = CreateXessContext(gpu, false);
        auto responsive = CreateXessContext(gpu, true);

        SyntheticFrame finalFrame;
        std::vector<double> baselineGpuMs;
        std::vector<double> responsiveGpuMs;
        baselineGpuMs.reserve(kFrames);
        responsiveGpuMs.reserve(kFrames);
        for (uint32_t frame = 0; frame < kFrames; ++frame) {
            auto input = MakeFrame(frame);
            UploadTexture(gpu, color.Get(), input.color.data(), static_cast<size_t>(kWidth) * 4,
                D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
            UploadTexture(gpu, velocity.Get(), reinterpret_cast<const uint8_t*>(input.velocity.data()),
                static_cast<size_t>(kWidth) * 4,
                D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
            UploadTexture(gpu, responsiveMask.Get(), input.responsiveMask.data(), static_cast<size_t>(kWidth),
                D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

            baselineGpuMs.push_back(ExecuteXess(
                gpu, baseline, color.Get(), velocity.Get(), nullptr, frame == 0, input.jitter));
            responsiveGpuMs.push_back(ExecuteXess(
                gpu, responsive, color.Get(), velocity.Get(), responsiveMask.Get(), frame == 0, input.jitter));
            if (frame + 1 == kFrames) {
                finalFrame = std::move(input);
            }
        }

        const auto baselineOutput = ReadbackRgba8(gpu, baseline.output.Get());
        const auto responsiveOutput = ReadbackRgba8(gpu, responsive.output.Get());
        const double baselineError = MeanAbsoluteRgbError(baselineOutput, finalFrame.color, finalFrame.changedMask);
        const double responsiveError = MeanAbsoluteRgbError(responsiveOutput, finalFrame.color, finalFrame.changedMask);
        const TimingStats baselineTiming = SummarizeTimings(baselineGpuMs, kTimingWarmupFrames);
        const TimingStats responsiveTiming = SummarizeTimings(responsiveGpuMs, kTimingWarmupFrames);
        const uint64_t baselineHash = HashBytes(baselineOutput);
        const uint64_t responsiveHash = HashBytes(responsiveOutput);

        if (baselineHash == 0 || responsiveHash == 0) {
            throw std::runtime_error("Unexpected zero output hash");
        }

        std::wcout << L"adapter=" << gpu.adapterDesc.Description << L"\n";
        std::cout << "xess_sdk_contract=3.0.2\n";
        std::cout << "mode=native_aa quality_setting=" << static_cast<int>(XESS_QUALITY_SETTING_AA)
                  << " input=" << kWidth << "x" << kHeight << " output=" << kWidth << "x" << kHeight << "\n";
        std::cout << "frames=" << kFrames
                  << " reset_frame=0 jitter=halton8 motion=current-to-previous-pixels jitter-included=0"
                  << " high_res_mv=1 mv_dilated=1 moving_pixels_final=" << finalFrame.movingPixels
                  << " motion_pixels_final=" << finalFrame.motionPixels << " ldr_input=1\n";
        std::cout << "temporary_heap_bytes baseline_buffer=" << baseline.properties.tempBufferHeapSize
                  << " baseline_texture=" << baseline.properties.tempTextureHeapSize
                  << " responsive_buffer=" << responsive.properties.tempBufferHeapSize
                  << " responsive_texture=" << responsive.properties.tempTextureHeapSize << "\n";
        std::cout << std::fixed << std::setprecision(4)
                  << "gpu_ms baseline_mean=" << baselineTiming.meanMs
                  << " baseline_min=" << baselineTiming.minMs
                  << " baseline_max=" << baselineTiming.maxMs
                  << " responsive_mean=" << responsiveTiming.meanMs
                  << " responsive_min=" << responsiveTiming.minMs
                  << " responsive_max=" << responsiveTiming.maxMs << "\n"
                  << "changed_region_mae baseline=" << baselineError
                  << " responsive=" << responsiveError << "\n";
        std::cout << std::hex
                  << "output_hash baseline=0x" << baselineHash
                  << " responsive=0x" << responsiveHash << std::dec << "\n";
        std::cout << "responsive_output_differs=" << (baselineHash != responsiveHash ? 1 : 0) << "\n";
        std::cout << "RESULT PASS\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "RESULT FAIL: " << error.what() << "\n";
        return 1;
    }
}
