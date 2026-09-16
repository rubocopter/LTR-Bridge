#include "common.h"
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <d3d11_4.h>
#include <dxgi1_4.h>
#include <iomanip>
#include <iostream>
#include <limits>
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
struct Arguments {
  HANDLE resource0_handle{};
  HANDLE done_fence_handle{};
  HANDLE control_read_handle{};
  HANDLE host_process_handle{};
  ltr::bridge_probe::GenerationSpec spec0{};
  std::wstring ready_fence_name;
  LUID luid{};
  std::uint32_t protocol = 0;
  std::uint32_t frames = 0;
  std::uint32_t expect_host_stall = 0;
  std::uint32_t expect_host_termination = 0;
};
[[nodiscard]] bool u32(const std::wstring &s, std::uint32_t &out) {
  try {
    const auto v = std::stoull(s);
    if (v > std::numeric_limits<std::uint32_t>::max())
      return false;
    out = static_cast<std::uint32_t>(v);
    return true;
  } catch (...) {
    return false;
  }
}
[[nodiscard]] bool parse(int argc, wchar_t **argv, Arguments &a) {
  bool low = false, high = false;
  for (int i = 1; i + 1 < argc; i += 2) {
    const std::wstring_view k(argv[i]);
    const std::wstring v(argv[i + 1]);
    if (k == L"--resource0-handle")
      a.resource0_handle =
          reinterpret_cast<HANDLE>(static_cast<std::uintptr_t>(std::stoull(v)));
    else if (k == L"--width0") {
      if (!u32(v, a.spec0.width))
        return false;
    } else if (k == L"--height0") {
      if (!u32(v, a.spec0.height))
        return false;
    } else if (k == L"--done-fence-handle")
      a.done_fence_handle =
          reinterpret_cast<HANDLE>(static_cast<std::uintptr_t>(std::stoull(v)));
    else if (k == L"--control-read-handle")
      a.control_read_handle =
          reinterpret_cast<HANDLE>(static_cast<std::uintptr_t>(std::stoull(v)));
    else if (k == L"--host-process-handle")
      a.host_process_handle =
          reinterpret_cast<HANDLE>(static_cast<std::uintptr_t>(std::stoull(v)));
    else if (k == L"--ready-fence-name")
      a.ready_fence_name = v;
    else if (k == L"--luid-low") {
      a.luid.LowPart = static_cast<DWORD>(std::stoul(v));
      low = true;
    } else if (k == L"--luid-high") {
      a.luid.HighPart = static_cast<LONG>(std::stol(v));
      high = true;
    } else if (k == L"--protocol-version") {
      if (!u32(v, a.protocol))
        return false;
    } else if (k == L"--frames-per-generation") {
      if (!u32(v, a.frames))
        return false;
    } else if (k == L"--expect-host-stall") {
      if (!u32(v, a.expect_host_stall) || a.expect_host_stall > 1U)
        return false;
    } else if (k == L"--expect-host-termination") {
      if (!u32(v, a.expect_host_termination) || a.expect_host_termination > 1U)
        return false;
    } else
      return false;
  }
  return a.resource0_handle && a.done_fence_handle && a.control_read_handle &&
         a.host_process_handle && a.spec0.width && a.spec0.height &&
         !a.ready_fence_name.empty() && low && high && a.frames;
}
[[nodiscard]] bool contract(ID3D11Texture2D *tex,
                            const ltr::bridge_probe::GenerationSpec &e,
                            std::uint32_t gen) {
  D3D11_TEXTURE2D_DESC d{};
  tex->GetDesc(&d);
  if (d.Width != e.width || d.Height != e.height ||
      d.Format != DXGI_FORMAT_R8G8B8A8_UNORM || d.MipLevels != 1 ||
      d.ArraySize != 1 || d.SampleDesc.Count != 1) {
    std::cerr << "reject=resource_contract generation=" << gen
              << " expected=" << e.width << "x" << e.height
              << " actual=" << d.Width << "x" << d.Height
              << " format=" << static_cast<unsigned>(d.Format)
              << " samples=" << d.SampleDesc.Count << "\n";
    return false;
  }
  return true;
}
} // namespace
int wmain(int argc, wchar_t **argv) {
  static_assert(sizeof(void *) == 4, "producer must be built x86");
  Arguments a{};
  if (!parse(argc, argv, a)) {
    std::cerr << "reject=arguments\n";
    return 2;
  }
  if (a.protocol != ltr::bridge_probe::kProtocolVersion) {
    std::cerr << "reject=protocol_version expected="
              << ltr::bridge_probe::kProtocolVersion << " actual=" << a.protocol
              << "\n";
    return 3;
  }
  ComPtr<IDXGIFactory4> factory;
  if (!check(CreateDXGIFactory2(0, IID_PPV_ARGS(&factory)),
             "CreateDXGIFactory2"))
    return 4;
  ComPtr<IDXGIAdapter> adapter;
  if (!check(factory->EnumAdapterByLuid(a.luid, IID_PPV_ARGS(&adapter)),
             "EnumAdapterByLuid")) {
    std::cerr << "reject=adapter_luid\n";
    return 5;
  }
  ComPtr<ID3D11Device> dev;
  ComPtr<ID3D11DeviceContext> ctx;
  D3D_FEATURE_LEVEL fl{};
  if (!check(D3D11CreateDevice(adapter.Get(), D3D_DRIVER_TYPE_UNKNOWN, nullptr,
                               D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0,
                               D3D11_SDK_VERSION, &dev, &fl, &ctx),
             "D3D11CreateDevice"))
    return 6;
  ComPtr<ID3D11Device1> dev1;
  ComPtr<ID3D11Device5> dev5;
  ComPtr<ID3D11DeviceContext4> ctx4;
  if (!check(dev.As(&dev1), "ID3D11Device1") ||
      !check(dev.As(&dev5), "ID3D11Device5") ||
      !check(ctx.As(&ctx4), "ID3D11DeviceContext4"))
    return 7;
  ComPtr<ID3D11Texture2D> shared[ltr::bridge_probe::kGenerationCount],
      staging[ltr::bridge_probe::kGenerationCount];
  ltr::bridge_probe::GenerationSpec
      specs[ltr::bridge_probe::kGenerationCount]{};
  specs[0] = a.spec0;
  if (!check(dev1->OpenSharedResource1(a.resource0_handle,
                                       IID_PPV_ARGS(&shared[0])),
             "OpenSharedResource1(generation0)"))
    return 8;
  CloseHandle(a.resource0_handle);
  a.resource0_handle = nullptr;
  if (!contract(shared[0].Get(), specs[0], 0))
    return 9;
  D3D11_TEXTURE2D_DESC d0{};
  shared[0]->GetDesc(&d0);
  d0.Usage = D3D11_USAGE_STAGING;
  d0.BindFlags = 0;
  d0.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
  d0.MiscFlags = 0;
  if (!check(dev->CreateTexture2D(&d0, nullptr, &staging[0]),
             "CreateTexture2D(staging0)"))
    return 10;
  ComPtr<ID3D11Fence> ready_fence, done_fence;
  if (!check(dev5->CreateFence(
                 0, D3D11_FENCE_FLAG_SHARED, __uuidof(ID3D11Fence),
                 reinterpret_cast<void **>(ready_fence.GetAddressOf())),
             "CreateFence(ready)"))
    return 11;
  HANDLE fh = nullptr;
  if (!check(ready_fence->CreateSharedHandle(nullptr, GENERIC_ALL,
                                             a.ready_fence_name.c_str(), &fh),
             "CreateSharedHandle(ready-fence)"))
    return 12;
  if (!check(dev5->OpenSharedFence(
                 a.done_fence_handle, __uuidof(ID3D11Fence),
                 reinterpret_cast<void **>(done_fence.GetAddressOf())),
             "OpenSharedFence(done)")) {
    CloseHandle(fh);
    return 12;
  }
  CloseHandle(a.done_fence_handle);
  a.done_fence_handle = nullptr;
  if (a.expect_host_termination) {
    if (!check(ctx4->Signal(ready_fence.Get(), 1),
               "Signal(host-termination-ready)")) {
      CloseHandle(a.host_process_handle);
      CloseHandle(fh);
      return 13;
    }
    ctx->Flush();
    HANDLE done_event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (!done_event) {
      CloseHandle(a.host_process_handle);
      CloseHandle(fh);
      return 13;
    }
    if (!check(done_fence->SetEventOnCompletion(1, done_event),
               "SetEventOnCompletion(host-termination)")) {
      CloseHandle(done_event);
      CloseHandle(a.host_process_handle);
      CloseHandle(fh);
      return 13;
    }
    HANDLE waits[] = {done_event, a.host_process_handle};
    const DWORD wait = WaitForMultipleObjects(2, waits, FALSE, 10000);
    const DWORD host_state = WaitForSingleObject(
        a.host_process_handle, wait == WAIT_OBJECT_0 ? 250 : 0);
    CloseHandle(done_event);
    CloseHandle(a.host_process_handle);
    a.host_process_handle = nullptr;
    CloseHandle(fh);
    if (host_state == WAIT_OBJECT_0) {
      std::cout << "reject=host_terminated detected_by=process_handle"
                   " fence_wait_result="
                << wait
                << "\n"
                   "RESULT PASS\n";
      return 22;
    }
    std::cerr << "host-termination probe failed wait=" << wait << "\n";
    return 23;
  }
  CloseHandle(a.host_process_handle);
  a.host_process_handle = nullptr;
  if (a.expect_host_stall) {
    if (!check(ctx4->Signal(ready_fence.Get(), 1),
               "Signal(host-stall-ready)")) {
      CloseHandle(fh);
      return 13;
    }
    ctx->Flush();
    HANDLE event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (!event) {
      CloseHandle(fh);
      return 13;
    }
    if (!check(done_fence->SetEventOnCompletion(1, event),
               "SetEventOnCompletion(host-stall)")) {
      CloseHandle(event);
      CloseHandle(fh);
      return 13;
    }
    const DWORD wait = WaitForSingleObject(event, 250);
    CloseHandle(event);
    CloseHandle(fh);
    if (wait == WAIT_TIMEOUT) {
      std::cout << "reject=host_stall timeout_ms=250\n";
      return 17;
    }
    std::cerr << "host-stall probe unexpectedly completed wait=" << wait
              << "\n";
    return 18;
  }
  std::uint64_t mismatches = 0;
  double sum = 0.0, minv = std::numeric_limits<double>::max(), maxv = 0.0;
  std::uint32_t frame = 0;
  for (std::uint32_t g = 0; g < ltr::bridge_probe::kGenerationCount; ++g) {
    if (g == 1) {
      staging[0].Reset();
      shared[0].Reset();
      ltr::bridge_probe::DynamicResourceMessage message{};
      DWORD bytes = 0;
      if (!ReadFile(a.control_read_handle, &message, sizeof(message), &bytes,
                    nullptr) ||
          bytes != sizeof(message)) {
        std::cerr << "reject=dynamic_control_read bytes=" << bytes
                  << " error=" << GetLastError() << "\n";
        CloseHandle(fh);
        return 19;
      }
      CloseHandle(a.control_read_handle);
      a.control_read_handle = nullptr;
      if (message.magic != ltr::bridge_probe::kControlMagic ||
          message.protocol != ltr::bridge_probe::kProtocolVersion ||
          message.generation != g || !message.width || !message.height ||
          message.resource_handle >
              static_cast<std::uint64_t>(
                  std::numeric_limits<std::uintptr_t>::max())) {
        std::cerr << "reject=dynamic_control_contract magic=" << message.magic
                  << " protocol=" << message.protocol
                  << " generation=" << message.generation << "\n";
        CloseHandle(fh);
        return 19;
      }
      specs[g] = {message.width, message.height};
      const HANDLE dynamic_handle = reinterpret_cast<HANDLE>(
          static_cast<std::uintptr_t>(message.resource_handle));
      if (!check(dev1->OpenSharedResource1(dynamic_handle,
                                           IID_PPV_ARGS(&shared[g])),
                 "OpenSharedResource1(dynamic)")) {
        CloseHandle(dynamic_handle);
        CloseHandle(fh);
        return 8;
      }
      CloseHandle(dynamic_handle);
      if (!contract(shared[g].Get(), specs[g], g)) {
        CloseHandle(fh);
        return 9;
      }
      D3D11_TEXTURE2D_DESC d{};
      shared[g]->GetDesc(&d);
      d.Usage = D3D11_USAGE_STAGING;
      d.BindFlags = 0;
      d.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
      d.MiscFlags = 0;
      if (!check(dev->CreateTexture2D(&d, nullptr, &staging[g]),
                 "CreateTexture2D(dynamic-staging)")) {
        CloseHandle(fh);
        return 10;
      }
      std::cout << "dynamic_resource_opened generation=" << g
                << " size=" << specs[g].width << "x" << specs[g].height << "\n";
    }
    const auto spec = specs[g];
    std::vector<std::uint8_t> p(static_cast<std::size_t>(spec.width) *
                                spec.height * 4U);
    for (std::uint32_t local = 0; local < a.frames; ++local, ++frame) {
      for (std::uint32_t y = 0; y < spec.height; ++y)
        for (std::uint32_t x = 0; x < spec.width; ++x) {
          const std::size_t i =
              (static_cast<std::size_t>(y) * spec.width + x) * 4U;
          p[i] = ltr::bridge_probe::source_r(x, y, frame);
          p[i + 1] = ltr::bridge_probe::source_g(x, y, frame);
          p[i + 2] = ltr::bridge_probe::source_b(x, y, frame);
          p[i + 3] = 255U;
        }
      ctx->UpdateSubresource(shared[g].Get(), 0, nullptr, p.data(),
                             spec.width * 4U, 0);
      const auto start = std::chrono::steady_clock::now();
      if (!check(ctx4->Signal(ready_fence.Get(),
                              static_cast<std::uint64_t>(frame) + 1ULL),
                 "Signal(producer-ready)")) {
        CloseHandle(fh);
        return 13;
      }
      ctx->Flush();
      if (!check(ctx4->Wait(done_fence.Get(),
                            static_cast<std::uint64_t>(frame) + 1ULL),
                 "Wait(consumer-done)")) {
        CloseHandle(fh);
        return 14;
      }
      ctx->CopyResource(staging[g].Get(), shared[g].Get());
      D3D11_MAPPED_SUBRESOURCE m{};
      if (!check(ctx->Map(staging[g].Get(), 0, D3D11_MAP_READ, 0, &m),
                 "Map(staging)")) {
        CloseHandle(fh);
        return 15;
      }
      const double ms = std::chrono::duration<double, std::milli>(
                            std::chrono::steady_clock::now() - start)
                            .count();
      sum += ms;
      minv = std::min(minv, ms);
      maxv = std::max(maxv, ms);
      for (std::uint32_t y = 0; y < spec.height; ++y) {
        const auto *row = static_cast<const std::uint8_t *>(m.pData) +
                          static_cast<std::size_t>(y) * m.RowPitch;
        for (std::uint32_t x = 0; x < spec.width; ++x) {
          const auto *q = row + static_cast<std::size_t>(x) * 4U;
          const auto er = static_cast<std::uint8_t>(
                         255U - ltr::bridge_probe::source_r(x, y, frame)),
                     eg = static_cast<std::uint8_t>(
                         255U - ltr::bridge_probe::source_g(x, y, frame)),
                     eb = static_cast<std::uint8_t>(
                         255U - ltr::bridge_probe::source_b(x, y, frame));
          if (q[0] != er || q[1] != eg || q[2] != eb || q[3] != 255U)
            ++mismatches;
        }
      }
      ctx->Unmap(staging[g].Get(), 0);
    }
  }
  CloseHandle(fh);
  const std::uint32_t total = a.frames * ltr::bridge_probe::kGenerationCount;
  std::cout << "producer_bitness=32 transport=open_host_created_d3d12_resource "
               "synchronization=shared_gpu_fence validation_cpu_readback=1\n"
            << "protocol_version=" << a.protocol
            << " generations=" << ltr::bridge_probe::kGenerationCount
            << " frames=" << total << " generation_size_transitions="
            << (ltr::bridge_probe::kGenerationCount - 1U)
            << " mismatches=" << mismatches << "\n"
            << std::fixed << std::setprecision(4)
            << "validation_round_trip_mean_ms=" << (sum / total)
            << " min_ms=" << minv << " max_ms=" << maxv << "\n";
  if (mismatches) {
    std::cout << "RESULT FAIL\n";
    return 16;
  }
  std::cout << "RESULT PASS\n";
  return 0;
}
