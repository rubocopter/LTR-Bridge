#include "common.h"

#include <windows.h>
#include <d3d11_4.h>
#include <dxgi1_4.h>
#include <wrl/client.h>

#include <cstdint>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

using Microsoft::WRL::ComPtr;

namespace {

[[nodiscard]] bool check(HRESULT result, const char* what) {
    if (SUCCEEDED(result)) {
        return true;
    }
    std::cerr << what << " failed hr=0x" << std::hex
              << static_cast<unsigned long>(result) << std::dec << "\n";
    return false;
}

struct Arguments {
    HANDLE resource_handle{};
    std::wstring fence_name;
    LUID luid{};
};

[[nodiscard]] bool parse_arguments(int argc, wchar_t** argv, Arguments& args) {
    bool low_seen = false;
    bool high_seen = false;
    for (int i = 1; i + 1 < argc; i += 2) {
        const std::wstring_view key(argv[i]);
        const std::wstring value(argv[i + 1]);
        if (key == L"--resource-handle") {
            args.resource_handle = reinterpret_cast<HANDLE>(
                static_cast<std::uintptr_t>(std::stoull(value)));
        } else if (key == L"--fence-name") {
            args.fence_name = value;
        } else if (key == L"--luid-low") {
            args.luid.LowPart = static_cast<DWORD>(std::stoul(value));
            low_seen = true;
        } else if (key == L"--luid-high") {
            args.luid.HighPart = static_cast<LONG>(std::stol(value));
            high_seen = true;
        } else {
            return false;
        }
    }
    return args.resource_handle != nullptr && !args.fence_name.empty() && low_seen && high_seen;
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
    static_assert(sizeof(void*) == 4, "producer must be built x86");

    Arguments args{};
    if (!parse_arguments(argc, argv, args)) {
        std::wcerr << L"invalid arguments\n";
        return 2;
    }

    ComPtr<IDXGIFactory4> factory;
    if (!check(CreateDXGIFactory2(0, IID_PPV_ARGS(&factory)), "CreateDXGIFactory2")) {
        return 3;
    }

    ComPtr<IDXGIAdapter> adapter;
    if (!check(factory->EnumAdapterByLuid(args.luid, IID_PPV_ARGS(&adapter)), "EnumAdapterByLuid")) {
        return 4;
    }

    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    D3D_FEATURE_LEVEL feature_level{};
    if (!check(D3D11CreateDevice(
            adapter.Get(),
            D3D_DRIVER_TYPE_UNKNOWN,
            nullptr,
            D3D11_CREATE_DEVICE_BGRA_SUPPORT,
            nullptr,
            0,
            D3D11_SDK_VERSION,
            &device,
            &feature_level,
            &context),
        "D3D11CreateDevice")) {
        return 5;
    }

    ComPtr<ID3D11Device1> device1;
    ComPtr<ID3D11Device5> device5;
    ComPtr<ID3D11DeviceContext4> context4;
    if (!check(device.As(&device1), "ID3D11Device1") ||
        !check(device.As(&device5), "ID3D11Device5") ||
        !check(context.As(&context4), "ID3D11DeviceContext4")) {
        return 6;
    }

    ComPtr<ID3D11Texture2D> shared_texture;
    if (!check(device1->OpenSharedResource1(
            args.resource_handle,
            IID_PPV_ARGS(&shared_texture)),
        "OpenSharedResource1")) {
        return 7;
    }

    ComPtr<ID3D11Fence> fence;
    if (!check(device5->CreateFence(
            0,
            D3D11_FENCE_FLAG_SHARED,
            __uuidof(ID3D11Fence),
            reinterpret_cast<void**>(fence.GetAddressOf())),
        "CreateFence")) {
        return 8;
    }
    HANDLE fence_handle = nullptr;
    if (!check(fence->CreateSharedHandle(
            nullptr,
            GENERIC_ALL,
            args.fence_name.c_str(),
            &fence_handle),
        "CreateSharedHandle(fence)")) {
        return 9;
    }

    std::vector<std::uint8_t> pixels(
        static_cast<std::size_t>(ltr::bridge_probe::kWidth) * ltr::bridge_probe::kHeight * 4U);
    for (std::uint32_t y = 0; y < ltr::bridge_probe::kHeight; ++y) {
        for (std::uint32_t x = 0; x < ltr::bridge_probe::kWidth; ++x) {
            const std::size_t i =
                (static_cast<std::size_t>(y) * ltr::bridge_probe::kWidth + x) * 4U;
            pixels[i + 0] = ltr::bridge_probe::source_r(x, y);
            pixels[i + 1] = ltr::bridge_probe::source_g(x, y);
            pixels[i + 2] = ltr::bridge_probe::source_b(x, y);
            pixels[i + 3] = 255U;
        }
    }

    context->UpdateSubresource(
        shared_texture.Get(),
        0,
        nullptr,
        pixels.data(),
        ltr::bridge_probe::kWidth * 4U,
        0);
    if (!check(context4->Signal(fence.Get(), ltr::bridge_probe::kProducerReadyFence),
        "Signal(producer-ready)")) {
        CloseHandle(fence_handle);
        return 10;
    }
    context->Flush();

    if (!check(context4->Wait(fence.Get(), ltr::bridge_probe::kConsumerDoneFence),
        "Wait(consumer-done)")) {
        CloseHandle(fence_handle);
        return 11;
    }

    D3D11_TEXTURE2D_DESC staging_desc{};
    shared_texture->GetDesc(&staging_desc);
    staging_desc.Usage = D3D11_USAGE_STAGING;
    staging_desc.BindFlags = 0;
    staging_desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    staging_desc.MiscFlags = 0;

    ComPtr<ID3D11Texture2D> staging;
    if (!check(device->CreateTexture2D(&staging_desc, nullptr, &staging),
        "CreateTexture2D(staging)")) {
        CloseHandle(fence_handle);
        return 12;
    }

    context->CopyResource(staging.Get(), shared_texture.Get());
    D3D11_MAPPED_SUBRESOURCE mapped{};
    if (!check(context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped), "Map(staging)")) {
        CloseHandle(fence_handle);
        return 13;
    }

    std::uint64_t mismatches = 0;
    for (std::uint32_t y = 0; y < ltr::bridge_probe::kHeight; ++y) {
        const auto* row = static_cast<const std::uint8_t*>(mapped.pData) +
            static_cast<std::size_t>(y) * mapped.RowPitch;
        for (std::uint32_t x = 0; x < ltr::bridge_probe::kWidth; ++x) {
            const auto* pixel = row + static_cast<std::size_t>(x) * 4U;
            const std::uint8_t expected_r =
                static_cast<std::uint8_t>(255U - ltr::bridge_probe::source_r(x, y));
            const std::uint8_t expected_g =
                static_cast<std::uint8_t>(255U - ltr::bridge_probe::source_g(x, y));
            const std::uint8_t expected_b =
                static_cast<std::uint8_t>(255U - ltr::bridge_probe::source_b(x, y));
            if (pixel[0] != expected_r || pixel[1] != expected_g ||
                pixel[2] != expected_b || pixel[3] != 255U) {
                ++mismatches;
            }
        }
    }
    context->Unmap(staging.Get(), 0);
    CloseHandle(fence_handle);

    std::cout << "producer_bitness=32 transport=open_host_created_d3d12_resource "
                 "synchronization=shared_gpu_fence validation_cpu_readback=1\n";
    std::cout << "pixels=" << (ltr::bridge_probe::kWidth * ltr::bridge_probe::kHeight)
              << " mismatches=" << mismatches << "\n";
    if (mismatches != 0) {
        std::cout << "RESULT FAIL\n";
        return 14;
    }

    std::cout << "RESULT PASS\n";
    return 0;
}
