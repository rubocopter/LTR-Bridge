#include "common.h"
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <d3d11_4.h>
#include <d3dcompiler.h>
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
  HANDLE resource1_handle{};
  HANDLE done_fence_handle{};
  HANDLE done_fence1_handle{};
  HANDLE control_read_handle{};
  HANDLE host_process_handle{};
  ltr::bridge_probe::GenerationSpec spec0{};
  std::wstring ready_fence_name;
  std::wstring ready_fence1_name;
  LUID luid{};
  std::uint32_t protocol = 0;
  std::uint32_t frames = 0;
  std::uint32_t expect_host_stall = 0;
  std::uint32_t expect_host_termination = 0;
  std::uint32_t expect_backpressure_host_termination = 0;
  std::uint32_t backpressure_depth = 0;
  std::uint32_t stereo_mode = 0;
  std::uint32_t stereo_history_mode = 0;
  std::uint32_t renderer_copy_mode = 0;
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
    else if (k == L"--resource1-handle")
      a.resource1_handle =
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
    else if (k == L"--done-fence1-handle")
      a.done_fence1_handle =
          reinterpret_cast<HANDLE>(static_cast<std::uintptr_t>(std::stoull(v)));
    else if (k == L"--control-read-handle")
      a.control_read_handle =
          reinterpret_cast<HANDLE>(static_cast<std::uintptr_t>(std::stoull(v)));
    else if (k == L"--host-process-handle")
      a.host_process_handle =
          reinterpret_cast<HANDLE>(static_cast<std::uintptr_t>(std::stoull(v)));
    else if (k == L"--ready-fence-name")
      a.ready_fence_name = v;
    else if (k == L"--ready-fence1-name")
      a.ready_fence1_name = v;
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
    } else if (k == L"--expect-backpressure-host-termination") {
      if (!u32(v, a.expect_backpressure_host_termination) ||
          a.expect_backpressure_host_termination > 1U)
        return false;
    } else if (k == L"--backpressure-depth") {
      if (!u32(v, a.backpressure_depth) || a.backpressure_depth > 2U)
        return false;
    } else if (k == L"--stereo-mode") {
      if (!u32(v, a.stereo_mode) || a.stereo_mode > 1U)
        return false;
    } else if (k == L"--stereo-history-mode") {
      if (!u32(v, a.stereo_history_mode) || a.stereo_history_mode > 1U)
        return false;
    } else if (k == L"--renderer-copy-mode") {
      if (!u32(v, a.renderer_copy_mode) || a.renderer_copy_mode > 3U)
        return false;
    } else
      return false;
  }
  const bool second_resource =
      (a.backpressure_depth != 2U && !a.stereo_mode) || a.resource1_handle;
  const bool stereo_contract =
      !a.stereo_mode || (a.done_fence1_handle && !a.ready_fence1_name.empty());
  return a.resource0_handle && second_resource && stereo_contract &&
         a.done_fence_handle && a.control_read_handle &&
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
[[nodiscard]] std::uint32_t pack_r10g10b10a2(std::uint8_t r, std::uint8_t g,
                                             std::uint8_t b) noexcept {
  const auto q10 = [](std::uint8_t v) {
    return (static_cast<std::uint32_t>(v) * 1023U + 127U) / 255U;
  };
  return q10(r) | (q10(g) << 10U) | (q10(b) << 20U) | (3U << 30U);
}
[[nodiscard]] ComPtr<ID3DBlob> compile_shader(const char *source,
                                              const char *entry,
                                              const char *target) {
  ComPtr<ID3DBlob> bytecode, errors;
  const HRESULT hr = D3DCompile(source, std::strlen(source), nullptr, nullptr,
                                nullptr, entry, target,
                                D3DCOMPILE_ENABLE_STRICTNESS, 0, &bytecode,
                                &errors);
  if (FAILED(hr)) {
    std::cerr << "D3DCompile(" << entry << ") failed hr=0x" << std::hex
              << static_cast<unsigned long>(hr) << std::dec;
    if (errors)
      std::cerr << " error=" << static_cast<const char *>(errors->GetBufferPointer());
    std::cerr << "\n";
    return nullptr;
  }
  return bytecode;
}
inline constexpr char kRendererBlitShader[] = R"(
Texture2D<float4> SourceTexture : register(t0);
struct VSOut {
  float4 position : SV_Position;
};
VSOut VSMain(uint id : SV_VertexID) {
  float2 p = float2((id << 1) & 2, id & 2);
  VSOut o;
  o.position = float4(p.x * 2.0 - 1.0, 1.0 - p.y * 2.0, 0.0, 1.0);
  return o;
}
float4 PSMain(VSOut input) : SV_Target {
  return SourceTexture.Load(int3(uint2(input.position.xy), 0));
}
)";
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
      staging[ltr::bridge_probe::kGenerationCount],
      renderer_source[ltr::bridge_probe::kGenerationCount],
      renderer_upload[ltr::bridge_probe::kGenerationCount];
  ComPtr<ID3D11ShaderResourceView>
      renderer_input[ltr::bridge_probe::kGenerationCount];
  ComPtr<ID3D11RenderTargetView>
      renderer_target[ltr::bridge_probe::kGenerationCount];
  ComPtr<ID3D11VertexShader> renderer_vs;
  ComPtr<ID3D11PixelShader> renderer_ps;
  UINT msaa4x_quality_levels = 0;
  if (a.renderer_copy_mode == 2U || a.renderer_copy_mode == 3U) {
    const auto vs = compile_shader(kRendererBlitShader, "VSMain", "vs_5_0");
    const auto ps = compile_shader(kRendererBlitShader, "PSMain", "ps_5_0");
    if (!vs || !ps ||
        !check(dev->CreateVertexShader(vs->GetBufferPointer(),
                                       vs->GetBufferSize(), nullptr,
                                       &renderer_vs),
               "CreateVertexShader(renderer-blit)") ||
        !check(dev->CreatePixelShader(ps->GetBufferPointer(),
                                      ps->GetBufferSize(), nullptr,
                                      &renderer_ps),
               "CreatePixelShader(renderer-blit)"))
      return 10;
  }
  if (a.renderer_copy_mode == 2U) {
    if (!check(dev->CheckMultisampleQualityLevels(
                   DXGI_FORMAT_R8G8B8A8_UNORM, 4, &msaa4x_quality_levels),
               "CheckMultisampleQualityLevels(renderer-msaa4x)") ||
        msaa4x_quality_levels == 0) {
      std::cerr << "renderer-msaa4x unsupported for R8G8B8A8_UNORM\n";
      return 10;
    }
  }
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
  const auto create_renderer_resources = [&](std::uint32_t generation) {
    if (!a.renderer_copy_mode)
      return true;
    D3D11_TEXTURE2D_DESC renderer_desc{};
    shared[generation]->GetDesc(&renderer_desc);
    renderer_desc.Usage = D3D11_USAGE_DEFAULT;
    renderer_desc.CPUAccessFlags = 0;
    renderer_desc.MiscFlags = 0;
    renderer_desc.SampleDesc.Count = 1;
    renderer_desc.SampleDesc.Quality = 0;
    if (a.renderer_copy_mode == 1U) {
      renderer_desc.BindFlags = 0;
      return check(dev->CreateTexture2D(&renderer_desc, nullptr,
                                        &renderer_source[generation]),
                   "CreateTexture2D(renderer-copy-source)");
    }
    if (a.renderer_copy_mode == 2U) {
      D3D11_TEXTURE2D_DESC upload_desc = renderer_desc;
      upload_desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
      if (!check(dev->CreateTexture2D(&upload_desc, nullptr,
                                      &renderer_upload[generation]),
                 "CreateTexture2D(renderer-msaa-upload)") ||
          !check(dev->CreateShaderResourceView(renderer_upload[generation].Get(),
                                               nullptr,
                                               &renderer_input[generation]),
                 "CreateShaderResourceView(renderer-msaa-upload)"))
        return false;
      renderer_desc.BindFlags = D3D11_BIND_RENDER_TARGET;
      renderer_desc.SampleDesc.Count = 4;
      renderer_desc.SampleDesc.Quality = 0;
      return check(dev->CreateTexture2D(&renderer_desc, nullptr,
                                        &renderer_source[generation]),
                   "CreateTexture2D(renderer-msaa4x-source)") &&
             check(dev->CreateRenderTargetView(renderer_source[generation].Get(),
                                               nullptr,
                                               &renderer_target[generation]),
                   "CreateRenderTargetView(renderer-msaa4x-source)");
    }
    renderer_desc.Format = DXGI_FORMAT_R10G10B10A2_UNORM;
    renderer_desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    return check(dev->CreateTexture2D(&renderer_desc, nullptr,
                                      &renderer_source[generation]),
                 "CreateTexture2D(renderer-r10-source)") &&
           check(dev->CreateShaderResourceView(renderer_source[generation].Get(),
                                               nullptr,
                                               &renderer_input[generation]),
                 "CreateShaderResourceView(renderer-r10-source)") &&
           check(dev->CreateRenderTargetView(shared[generation].Get(), nullptr,
                                             &renderer_target[generation]),
                 "CreateRenderTargetView(shared-rgba8)");
  };
  if (!create_renderer_resources(0))
    return 10;
  if (a.backpressure_depth == 2U || a.stereo_mode) {
    specs[1] = a.spec0;
    if (!check(dev1->OpenSharedResource1(a.resource1_handle,
                                         IID_PPV_ARGS(&shared[1])),
               a.stereo_mode ? "OpenSharedResource1(stereo-right)"
                             : "OpenSharedResource1(backpressure-slot1)"))
      return 8;
    CloseHandle(a.resource1_handle);
    a.resource1_handle = nullptr;
    if (!contract(shared[1].Get(), specs[1], 1))
      return 9;
    if (!check(dev->CreateTexture2D(&d0, nullptr, &staging[1]),
               a.stereo_mode ? "CreateTexture2D(stereo-staging-right)"
                             : "CreateTexture2D(backpressure-staging1)"))
      return 10;
  }
  ComPtr<ID3D11Fence> ready_fence, done_fence, ready_fence1, done_fence1;
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
  HANDLE fh1 = nullptr;
  if (a.stereo_mode) {
    if (!check(dev5->CreateFence(
                   0, D3D11_FENCE_FLAG_SHARED, __uuidof(ID3D11Fence),
                   reinterpret_cast<void **>(ready_fence1.GetAddressOf())),
               "CreateFence(stereo-ready1)")) {
      CloseHandle(fh);
      return 11;
    }
    if (!check(ready_fence1->CreateSharedHandle(
                   nullptr, GENERIC_ALL, a.ready_fence1_name.c_str(), &fh1),
               "CreateSharedHandle(stereo-ready1)")) {
      CloseHandle(fh);
      return 12;
    }
    if (!check(dev5->OpenSharedFence(
                   a.done_fence1_handle, __uuidof(ID3D11Fence),
                   reinterpret_cast<void **>(done_fence1.GetAddressOf())),
               "OpenSharedFence(stereo-done1)")) {
      CloseHandle(fh1);
      CloseHandle(fh);
      return 12;
    }
    CloseHandle(a.done_fence1_handle);
    a.done_fence1_handle = nullptr;
  }
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
  if (!a.expect_backpressure_host_termination) {
    CloseHandle(a.host_process_handle);
    a.host_process_handle = nullptr;
  }
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
  if (a.stereo_mode) {
    CloseHandle(a.control_read_handle);
    a.control_read_handle = nullptr;
    const auto spec = a.spec0;
    const std::uint8_t eye_marker[2] = {0x4CU, 0xD3U};
    std::uint64_t mismatches = 0, cross_eye_contamination = 0;
    double wait_sum_ms = 0.0;
    std::vector<std::uint8_t> pixels(static_cast<std::size_t>(spec.width) *
                                     spec.height * 4U);
    for (std::uint32_t temporal = 0; temporal < a.frames; ++temporal) {
      for (std::uint32_t eye = 0; eye < 2U; ++eye) {
        const std::uint32_t seed_frame = temporal + eye * 997U;
        const std::uint32_t expected_seed_frame =
            a.stereo_history_mode ? eye * 997U : seed_frame;
        for (std::uint32_t y = 0; y < spec.height; ++y)
          for (std::uint32_t x = 0; x < spec.width; ++x) {
            const std::size_t i =
                (static_cast<std::size_t>(y) * spec.width + x) * 4U;
            pixels[i] = ltr::bridge_probe::source_r(x, y, seed_frame);
            pixels[i + 1] = ltr::bridge_probe::source_g(x, y, seed_frame);
            pixels[i + 2] = ltr::bridge_probe::source_b(x, y, seed_frame);
            pixels[i + 3] = eye_marker[eye];
          }
        ctx->UpdateSubresource(shared[eye].Get(), 0, nullptr, pixels.data(),
                               spec.width * 4U, 0);
        ID3D11Fence *ready = eye == 0 ? ready_fence.Get() : ready_fence1.Get();
        ID3D11Fence *done = eye == 0 ? done_fence.Get() : done_fence1.Get();
        const std::uint64_t fence_value =
            static_cast<std::uint64_t>(temporal) + 1ULL;
        if (!check(ctx4->Signal(ready, fence_value),
                   eye == 0 ? "Signal(stereo-left-ready)"
                            : "Signal(stereo-right-ready)")) {
          CloseHandle(fh1);
          CloseHandle(fh);
          return 20;
        }
        ctx->Flush();
        const auto wait_start = std::chrono::steady_clock::now();
        if (!check(ctx4->Wait(done, fence_value),
                   eye == 0 ? "Wait(stereo-left-done)"
                            : "Wait(stereo-right-done)")) {
          CloseHandle(fh1);
          CloseHandle(fh);
          return 20;
        }
        ctx->CopyResource(staging[eye].Get(), shared[eye].Get());
        D3D11_MAPPED_SUBRESOURCE mapped{};
        if (!check(ctx->Map(staging[eye].Get(), 0, D3D11_MAP_READ, 0, &mapped),
                   eye == 0 ? "Map(stereo-left)" : "Map(stereo-right)")) {
          CloseHandle(fh1);
          CloseHandle(fh);
          return 20;
        }
        wait_sum_ms += std::chrono::duration<double, std::milli>(
                           std::chrono::steady_clock::now() - wait_start)
                           .count();
        for (std::uint32_t y = 0; y < spec.height; ++y) {
          const auto *row = static_cast<const std::uint8_t *>(mapped.pData) +
                            static_cast<std::size_t>(y) * mapped.RowPitch;
          for (std::uint32_t x = 0; x < spec.width; ++x) {
            const auto *q = row + static_cast<std::size_t>(x) * 4U;
            const auto er = static_cast<std::uint8_t>(
                           255U -
                           ltr::bridge_probe::source_r(x, y,
                                                       expected_seed_frame)),
                       eg = static_cast<std::uint8_t>(
                           255U -
                           ltr::bridge_probe::source_g(x, y,
                                                       expected_seed_frame)),
                       eb = static_cast<std::uint8_t>(
                           255U -
                           ltr::bridge_probe::source_b(x, y,
                                                       expected_seed_frame));
            if (q[3] == eye_marker[1U - eye])
              ++cross_eye_contamination;
            if (q[0] != er || q[1] != eg || q[2] != eb ||
                q[3] != eye_marker[eye])
              ++mismatches;
          }
        }
        ctx->Unmap(staging[eye].Get(), 0);
      }
    }
    CloseHandle(fh1);
    CloseHandle(fh);
    const std::uint32_t view_frames = a.frames * 2U;
    const bool passed = mismatches == 0 && cross_eye_contamination == 0;
    std::cout << "producer_bitness=32 mode="
              << (a.stereo_history_mode ? "stereo-history" : "stereo")
              << " eyes=2 temporal_frames="
              << a.frames << " view_frames=" << view_frames
              << " mismatches=" << mismatches
              << " cross_eye_contamination=" << cross_eye_contamination << "\n"
              << std::fixed << std::setprecision(4)
              << "validation_wait_readback_mean_ms="
              << (wait_sum_ms / static_cast<double>(view_frames)) << "\n"
              << (passed ? "RESULT PASS\n" : "RESULT FAIL\n");
    return passed ? 0 : 25;
  }
  if (a.backpressure_depth) {
    CloseHandle(a.control_read_handle);
    a.control_read_handle = nullptr;
    const std::uint32_t depth = a.backpressure_depth;
    const std::uint32_t total = a.frames * ltr::bridge_probe::kGenerationCount;
    const auto spec = a.spec0;
    std::vector<std::uint8_t> pixels(static_cast<std::size_t>(spec.width) *
                                     spec.height * 4U);
    std::uint64_t mismatches = 0;
    std::uint32_t submitted = 0, completed = 0, max_in_flight = 0;
    std::uint32_t pending_reuse_waits = 0;
    bool backpressure_host_terminated = false;
    double wait_sum_ms = 0.0;

    const auto validate = [&](std::uint32_t frame_to_validate,
                              std::uint32_t slot, bool reuse) -> bool {
      const std::uint64_t target =
          static_cast<std::uint64_t>(frame_to_validate) + 1ULL;
      if (reuse && done_fence->GetCompletedValue() < target)
        ++pending_reuse_waits;
      const auto wait_start = std::chrono::steady_clock::now();
      if (reuse && a.expect_backpressure_host_termination) {
        HANDLE done_event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        if (!done_event)
          return false;
        if (!check(done_fence->SetEventOnCompletion(target, done_event),
                   "SetEventOnCompletion(backpressure-host-termination)")) {
          CloseHandle(done_event);
          return false;
        }
        HANDLE waits[] = {done_event, a.host_process_handle};
        const DWORD wait = WaitForMultipleObjects(2, waits, FALSE, 10000);
        const DWORD host_state = WaitForSingleObject(
            a.host_process_handle, wait == WAIT_OBJECT_0 ? 250 : 0);
        CloseHandle(done_event);
        if (host_state == WAIT_OBJECT_0) {
          backpressure_host_terminated = true;
          std::cout << "reject=backpressure_host_terminated "
                       "detected_by=process_handle frame="
                    << frame_to_validate << " slot=" << slot
                    << " target=" << target << " fence_wait_result=" << wait
                    << "\nRESULT PASS\n";
          return false;
        }
        std::cerr << "backpressure-host-termination probe failed wait=" << wait
                  << " host_state=" << host_state << "\n";
        return false;
      }
      if (!check(ctx4->Wait(done_fence.Get(), target),
                 "Wait(backpressure-done)"))
        return false;
      ctx->CopyResource(staging[slot].Get(), shared[slot].Get());
      D3D11_MAPPED_SUBRESOURCE mapped{};
      if (!check(ctx->Map(staging[slot].Get(), 0, D3D11_MAP_READ, 0, &mapped),
                 "Map(backpressure-staging)"))
        return false;
      wait_sum_ms += std::chrono::duration<double, std::milli>(
                         std::chrono::steady_clock::now() - wait_start)
                         .count();
      for (std::uint32_t y = 0; y < spec.height; ++y) {
        const auto *row = static_cast<const std::uint8_t *>(mapped.pData) +
                          static_cast<std::size_t>(y) * mapped.RowPitch;
        for (std::uint32_t x = 0; x < spec.width; ++x) {
          const auto *q = row + static_cast<std::size_t>(x) * 4U;
          const auto er = static_cast<std::uint8_t>(
                         255U -
                         ltr::bridge_probe::source_r(x, y, frame_to_validate)),
                     eg = static_cast<std::uint8_t>(
                         255U -
                         ltr::bridge_probe::source_g(x, y, frame_to_validate)),
                     eb = static_cast<std::uint8_t>(
                         255U -
                         ltr::bridge_probe::source_b(x, y, frame_to_validate));
          if (q[0] != er || q[1] != eg || q[2] != eb || q[3] != 255U)
            ++mismatches;
        }
      }
      ctx->Unmap(staging[slot].Get(), 0);
      ++completed;
      return true;
    };

    for (std::uint32_t frame = 0; frame < total; ++frame) {
      const std::uint32_t slot = frame % depth;
      if (frame >= depth && !validate(frame - depth, slot, true)) {
        if (a.host_process_handle) {
          CloseHandle(a.host_process_handle);
          a.host_process_handle = nullptr;
        }
        CloseHandle(fh);
        return backpressure_host_terminated ? 26 : 20;
      }
      for (std::uint32_t y = 0; y < spec.height; ++y)
        for (std::uint32_t x = 0; x < spec.width; ++x) {
          const std::size_t i =
              (static_cast<std::size_t>(y) * spec.width + x) * 4U;
          pixels[i] = ltr::bridge_probe::source_r(x, y, frame);
          pixels[i + 1] = ltr::bridge_probe::source_g(x, y, frame);
          pixels[i + 2] = ltr::bridge_probe::source_b(x, y, frame);
          pixels[i + 3] = 255U;
        }
      ctx->UpdateSubresource(shared[slot].Get(), 0, nullptr, pixels.data(),
                             spec.width * 4U, 0);
      if (!check(ctx4->Signal(ready_fence.Get(),
                              static_cast<std::uint64_t>(frame) + 1ULL),
                 "Signal(backpressure-ready)")) {
        CloseHandle(fh);
        return 20;
      }
      ctx->Flush();
      ++submitted;
      max_in_flight = std::max(max_in_flight, submitted - completed);
    }
    for (std::uint32_t frame = total - depth; frame < total; ++frame) {
      if (!validate(frame, frame % depth, false)) {
        if (a.host_process_handle) {
          CloseHandle(a.host_process_handle);
          a.host_process_handle = nullptr;
        }
        CloseHandle(fh);
        return 20;
      }
    }
    if (a.host_process_handle) {
      CloseHandle(a.host_process_handle);
      a.host_process_handle = nullptr;
    }
    CloseHandle(fh);
    const bool passed = mismatches == 0 && max_in_flight == depth &&
                        pending_reuse_waits > 0 && completed == total;
    std::cout << "producer_bitness=32 mode=backpressure ring_depth=" << depth
              << " frames=" << total << " max_in_flight=" << max_in_flight
              << " pending_reuse_waits=" << pending_reuse_waits
              << " mismatches=" << mismatches << "\n"
              << std::fixed << std::setprecision(4)
              << "validation_wait_readback_mean_ms="
              << (wait_sum_ms / static_cast<double>(total)) << "\n"
              << (passed ? "RESULT PASS\n" : "RESULT FAIL\n");
    return passed ? 0 : 24;
  }
  std::uint64_t mismatches = 0;
  double sum = 0.0, minv = std::numeric_limits<double>::max(), maxv = 0.0;
  std::uint32_t frame = 0;
  const std::uint32_t total = a.frames * ltr::bridge_probe::kGenerationCount;
  ComPtr<ID3D11Query> copy_disjoint;
  std::vector<ComPtr<ID3D11Query>> copy_start(total), copy_end(total);
  if (a.renderer_copy_mode) {
    D3D11_QUERY_DESC disjoint_desc{};
    disjoint_desc.Query = D3D11_QUERY_TIMESTAMP_DISJOINT;
    if (!check(dev->CreateQuery(&disjoint_desc, &copy_disjoint),
               "CreateQuery(renderer-copy-disjoint)")) {
      CloseHandle(fh);
      return 27;
    }
    D3D11_QUERY_DESC timestamp_desc{};
    timestamp_desc.Query = D3D11_QUERY_TIMESTAMP;
    for (std::uint32_t i = 0; i < total; ++i) {
      if (!check(dev->CreateQuery(&timestamp_desc, &copy_start[i]),
                 "CreateQuery(renderer-copy-start)") ||
          !check(dev->CreateQuery(&timestamp_desc, &copy_end[i]),
                 "CreateQuery(renderer-copy-end)")) {
        CloseHandle(fh);
        return 27;
      }
    }
    ctx->Begin(copy_disjoint.Get());
  }
  const auto draw_renderer_blit = [&](std::uint32_t generation) {
    const auto spec = specs[generation];
    D3D11_VIEWPORT viewport{};
    viewport.Width = static_cast<float>(spec.width);
    viewport.Height = static_cast<float>(spec.height);
    viewport.MaxDepth = 1.0f;
    ctx->RSSetViewports(1, &viewport);
    ID3D11RenderTargetView *rtv = renderer_target[generation].Get();
    ctx->OMSetRenderTargets(1, &rtv, nullptr);
    ctx->IASetInputLayout(nullptr);
    ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ctx->VSSetShader(renderer_vs.Get(), nullptr, 0);
    ctx->PSSetShader(renderer_ps.Get(), nullptr, 0);
    ID3D11ShaderResourceView *srv = renderer_input[generation].Get();
    ctx->PSSetShaderResources(0, 1, &srv);
    ctx->Draw(3, 0);
    ID3D11ShaderResourceView *null_srv = nullptr;
    ctx->PSSetShaderResources(0, 1, &null_srv);
    ID3D11RenderTargetView *null_rtv = nullptr;
    ctx->OMSetRenderTargets(1, &null_rtv, nullptr);
  };
  for (std::uint32_t g = 0; g < ltr::bridge_probe::kGenerationCount; ++g) {
    if (g == 1) {
      renderer_target[0].Reset();
      renderer_input[0].Reset();
      renderer_upload[0].Reset();
      renderer_source[0].Reset();
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
      if (!create_renderer_resources(g)) {
        CloseHandle(fh);
        return 10;
      }
      std::cout << "dynamic_resource_opened generation=" << g
                << " size=" << specs[g].width << "x" << specs[g].height << "\n";
    }
    const auto spec = specs[g];
    std::vector<std::uint8_t> p(static_cast<std::size_t>(spec.width) *
                                spec.height * 4U);
    std::vector<std::uint32_t> p10;
    if (a.renderer_copy_mode == 3U)
      p10.resize(static_cast<std::size_t>(spec.width) * spec.height);
    for (std::uint32_t local = 0; local < a.frames; ++local, ++frame) {
      for (std::uint32_t y = 0; y < spec.height; ++y)
        for (std::uint32_t x = 0; x < spec.width; ++x) {
          const std::size_t i =
              (static_cast<std::size_t>(y) * spec.width + x) * 4U;
          p[i] = ltr::bridge_probe::source_r(x, y, frame);
          p[i + 1] = ltr::bridge_probe::source_g(x, y, frame);
          p[i + 2] = ltr::bridge_probe::source_b(x, y, frame);
          p[i + 3] = 255U;
          if (a.renderer_copy_mode == 3U)
            p10[static_cast<std::size_t>(y) * spec.width + x] =
                pack_r10g10b10a2(p[i], p[i + 1], p[i + 2]);
        }
      if (a.renderer_copy_mode == 1U) {
        ctx->UpdateSubresource(renderer_source[g].Get(), 0, nullptr, p.data(),
                               spec.width * 4U, 0);
        ctx->End(copy_start[frame].Get());
        ctx->CopyResource(shared[g].Get(), renderer_source[g].Get());
        ctx->End(copy_end[frame].Get());
      } else if (a.renderer_copy_mode == 2U) {
        ctx->UpdateSubresource(renderer_upload[g].Get(), 0, nullptr, p.data(),
                               spec.width * 4U, 0);
        draw_renderer_blit(g);
        ctx->End(copy_start[frame].Get());
        ctx->ResolveSubresource(shared[g].Get(), 0, renderer_source[g].Get(), 0,
                                DXGI_FORMAT_R8G8B8A8_UNORM);
        ctx->End(copy_end[frame].Get());
      } else if (a.renderer_copy_mode == 3U) {
        ctx->UpdateSubresource(renderer_source[g].Get(), 0, nullptr, p10.data(),
                               spec.width * 4U, 0);
        ctx->End(copy_start[frame].Get());
        draw_renderer_blit(g);
        ctx->End(copy_end[frame].Get());
      } else {
        ctx->UpdateSubresource(shared[g].Get(), 0, nullptr, p.data(),
                               spec.width * 4U, 0);
      }
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
  double copy_sum_us = 0.0;
  double copy_min_us = std::numeric_limits<double>::max();
  double copy_max_us = 0.0;
  double generation_copy_sum_us[ltr::bridge_probe::kGenerationCount]{};
  if (a.renderer_copy_mode) {
    ctx->End(copy_disjoint.Get());
    ctx->Flush();
    const auto wait_query = [&](ID3D11Asynchronous *query, void *data,
                                UINT size, const char *what) -> bool {
      const auto deadline =
          std::chrono::steady_clock::now() + std::chrono::seconds(10);
      for (;;) {
        const HRESULT hr = ctx->GetData(query, data, size, 0);
        if (hr == S_OK)
          return true;
        if (FAILED(hr))
          return check(hr, what);
        if (std::chrono::steady_clock::now() >= deadline) {
          std::cerr << what << " timed out\n";
          return false;
        }
        Sleep(1);
      }
    };
    D3D11_QUERY_DATA_TIMESTAMP_DISJOINT disjoint{};
    if (!wait_query(copy_disjoint.Get(), &disjoint, sizeof(disjoint),
                    "GetData(renderer-copy-disjoint)") ||
        disjoint.Disjoint || disjoint.Frequency == 0) {
      CloseHandle(fh);
      std::cerr << "renderer-copy timestamp stream disjoint\n";
      return 27;
    }
    for (std::uint32_t i = 0; i < total; ++i) {
      std::uint64_t begin = 0, end = 0;
      if (!wait_query(copy_start[i].Get(), &begin, sizeof(begin),
                      "GetData(renderer-copy-start)") ||
          !wait_query(copy_end[i].Get(), &end, sizeof(end),
                      "GetData(renderer-copy-end)")) {
        CloseHandle(fh);
        return 27;
      }
      const double us = static_cast<double>(end - begin) * 1000000.0 /
                        static_cast<double>(disjoint.Frequency);
      copy_sum_us += us;
      copy_min_us = std::min(copy_min_us, us);
      copy_max_us = std::max(copy_max_us, us);
      generation_copy_sum_us[i / a.frames] += us;
    }
  }
  CloseHandle(fh);
  const char *mode_name = "multiframe";
  const char *transfer_kind = "none";
  const char *source_format = "R8G8B8A8_UNORM";
  std::uint32_t source_samples = 1U;
  if (a.renderer_copy_mode == 1U) {
    mode_name = "renderer-copy";
    transfer_kind = "CopyResource";
  } else if (a.renderer_copy_mode == 2U) {
    mode_name = "renderer-resolve";
    transfer_kind = "ResolveSubresource";
    source_samples = 4U;
  } else if (a.renderer_copy_mode == 3U) {
    mode_name = "renderer-convert";
    transfer_kind = "fullscreen-shader";
    source_format = "R10G10B10A2_UNORM";
  }
  std::cout << "producer_bitness=32 mode=" << mode_name
            << " transport=open_host_created_d3d12_resource "
               "synchronization=shared_gpu_fence validation_cpu_readback=1\n"
            << "protocol_version=" << a.protocol
            << " generations=" << ltr::bridge_probe::kGenerationCount
            << " frames=" << total << " generation_size_transitions="
            << (ltr::bridge_probe::kGenerationCount - 1U)
            << " mismatches=" << mismatches
            << " renderer_source_format=" << source_format
            << " renderer_source_samples=" << source_samples
            << " renderer_transfer_kind=" << transfer_kind << "\n"
            << std::fixed << std::setprecision(4)
            << "validation_round_trip_mean_ms=" << (sum / total)
            << " min_ms=" << minv << " max_ms=" << maxv << "\n";
  if (a.renderer_copy_mode) {
    std::cout << "renderer_to_shared_gpu_transfers=" << total
              << " renderer_to_shared_gpu_transfer_mean_us="
              << (copy_sum_us / static_cast<double>(total))
              << " min_us=" << copy_min_us << " max_us=" << copy_max_us
              << "\n";
    for (std::uint32_t g = 0; g < ltr::bridge_probe::kGenerationCount; ++g)
      std::cout << "renderer_transfer_generation=" << g << " size="
                << specs[g].width << "x" << specs[g].height << " mean_us="
                << (generation_copy_sum_us[g] / static_cast<double>(a.frames))
                << "\n";
  }
  if (mismatches) {
    std::cout << "RESULT FAIL\n";
    return 16;
  }
  std::cout << "RESULT PASS\n";
  return 0;
}
