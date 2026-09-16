#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <d3d11.h>
#include <d3d9.h>
#include <dxgi.h>
#include <iomanip>
#include <iostream>
#include <limits>
#include <string_view>
#include <thread>
#include <windows.h>
#include <wrl/client.h>

using Microsoft::WRL::ComPtr;

namespace {

struct GenerationSpec {
  UINT width;
  UINT height;
};

constexpr GenerationSpec kGenerations[] = {{64, 64}, {96, 72}};
constexpr std::uint32_t kFramesPerGeneration = 6;

struct FormatCase {
  const char *name;
  D3DFORMAT d3d9_format;
  DXGI_FORMAT dxgi_format;
  bool documented;
};

constexpr FormatCase kFormats[] = {
    {"rgba8", D3DFMT_A8B8G8R8, DXGI_FORMAT_R8G8B8A8_UNORM, true},
    {"rgb10a2", D3DFMT_A2B10G10R10, DXGI_FORMAT_R10G10B10A2_UNORM, true},
    {"rgba16f", D3DFMT_A16B16G16R16F, DXGI_FORMAT_R16G16B16A16_FLOAT, true},
    {"bgra8-control", D3DFMT_A8R8G8B8, DXGI_FORMAT_B8G8R8A8_UNORM, false},
};

[[nodiscard]] bool check(HRESULT hr, const char *what) {
  if (SUCCEEDED(hr))
    return true;
  std::cerr << what << " failed hr=0x" << std::hex
            << static_cast<unsigned long>(hr) << std::dec << "\n";
  return false;
}

[[nodiscard]] HWND create_hidden_window() {
  const wchar_t *class_name = L"LTRD3D9ShareProbeWindow";
  WNDCLASSEXW wc{};
  wc.cbSize = sizeof(wc);
  wc.lpfnWndProc = DefWindowProcW;
  wc.hInstance = GetModuleHandleW(nullptr);
  wc.lpszClassName = class_name;
  if (!RegisterClassExW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
    return nullptr;
  return CreateWindowExW(0, class_name, L"LTR D3D9 share probe", WS_OVERLAPPED,
                         0, 0, 32, 32, nullptr, nullptr, wc.hInstance, nullptr);
}

[[nodiscard]] bool wait_d3d9_event(IDirect3DDevice9 *device) {
  ComPtr<IDirect3DQuery9> query;
  if (!check(device->CreateQuery(D3DQUERYTYPE_EVENT, &query),
             "CreateQuery(D3DQUERYTYPE_EVENT)") ||
      !check(query->Issue(D3DISSUE_END), "Issue(D3DQUERYTYPE_EVENT)"))
    return false;
  const auto deadline =
      std::chrono::steady_clock::now() + std::chrono::seconds(5);
  while (std::chrono::steady_clock::now() < deadline) {
    const HRESULT hr = query->GetData(nullptr, 0, D3DGETDATA_FLUSH);
    if (hr == S_OK)
      return true;
    if (FAILED(hr))
      return check(hr, "GetData(D3DQUERYTYPE_EVENT)");
    std::this_thread::yield();
  }
  std::cerr << "GetData(D3DQUERYTYPE_EVENT) timed out\n";
  return false;
}

[[nodiscard]] float half_to_float(std::uint16_t h) noexcept {
  const std::uint32_t sign = static_cast<std::uint32_t>(h & 0x8000U) << 16U;
  std::uint32_t exponent = (h >> 10U) & 0x1FU;
  std::uint32_t mantissa = h & 0x03FFU;
  std::uint32_t bits = 0;
  if (exponent == 0) {
    if (mantissa == 0) {
      bits = sign;
    } else {
      int shift = 0;
      while ((mantissa & 0x0400U) == 0) {
        mantissa <<= 1U;
        ++shift;
      }
      mantissa &= 0x03FFU;
      const std::uint32_t e = static_cast<std::uint32_t>(127 - 15 - shift);
      bits = sign | (e << 23U) | (mantissa << 13U);
    }
  } else if (exponent == 31U) {
    bits = sign | 0x7F800000U | (mantissa << 13U);
  } else {
    exponent += 127U - 15U;
    bits = sign | (exponent << 23U) | (mantissa << 13U);
  }
  float value = 0.0f;
  std::memcpy(&value, &bits, sizeof(value));
  return value;
}

[[nodiscard]] std::uint16_t float_to_half(float value) noexcept {
  std::uint32_t bits = 0;
  std::memcpy(&bits, &value, sizeof(bits));
  const std::uint32_t sign = (bits >> 16U) & 0x8000U;
  const int exponent = static_cast<int>((bits >> 23U) & 0xFFU) - 127 + 15;
  const std::uint32_t mantissa = bits & 0x7FFFFFU;
  if (exponent <= 0)
    return static_cast<std::uint16_t>(sign);
  if (exponent >= 31)
    return static_cast<std::uint16_t>(sign | 0x7C00U);
  return static_cast<std::uint16_t>(
      sign | (static_cast<std::uint32_t>(exponent) << 10U) |
      ((mantissa + 0x1000U) >> 13U));
}

struct ExpectedColor {
  std::uint8_t r;
  std::uint8_t g;
  std::uint8_t b;
  std::uint8_t a;
};

[[nodiscard]] ExpectedColor frame_color(std::uint32_t frame) noexcept {
  return {static_cast<std::uint8_t>(31U + frame * 13U),
          static_cast<std::uint8_t>(71U + frame * 7U),
          static_cast<std::uint8_t>(113U + frame * 5U), 255U};
}

[[nodiscard]] D3DCOLOR d3d_color(ExpectedColor c) noexcept {
  return D3DCOLOR_ARGB(c.a, c.r, c.g, c.b);
}

[[nodiscard]] bool validate_pixel(const FormatCase &format,
                                  const std::uint8_t *pixel,
                                  ExpectedColor expected) noexcept {
  if (format.dxgi_format == DXGI_FORMAT_R8G8B8A8_UNORM)
    return pixel[0] == expected.r && pixel[1] == expected.g &&
           pixel[2] == expected.b && pixel[3] == expected.a;
  if (format.dxgi_format == DXGI_FORMAT_B8G8R8A8_UNORM)
    return pixel[0] == expected.b && pixel[1] == expected.g &&
           pixel[2] == expected.r && pixel[3] == expected.a;
  if (format.dxgi_format == DXGI_FORMAT_R10G10B10A2_UNORM) {
    std::uint32_t packed = 0;
    std::memcpy(&packed, pixel, sizeof(packed));
    const auto quantize10 = [](std::uint8_t value) {
      return (static_cast<std::uint32_t>(value) * 1023U + 127U) / 255U;
    };
    return (packed & 0x3FFU) == quantize10(expected.r) &&
           ((packed >> 10U) & 0x3FFU) == quantize10(expected.g) &&
           ((packed >> 20U) & 0x3FFU) == quantize10(expected.b) &&
           ((packed >> 30U) & 0x3U) == 3U;
  }
  const auto *halves = reinterpret_cast<const std::uint16_t *>(pixel);
  const float expected_channels[] = {
      static_cast<float>(expected.r) / 255.0f,
      static_cast<float>(expected.g) / 255.0f,
      static_cast<float>(expected.b) / 255.0f, 1.0f};
  for (int i = 0; i < 4; ++i)
    if (std::abs(half_to_float(halves[i]) - expected_channels[i]) > 0.0015f)
      return false;
  return true;
}

[[nodiscard]] bool fill_upload_texture(IDirect3DTexture9 *texture,
                                       const FormatCase &format,
                                       ExpectedColor color, UINT width,
                                       UINT height) {
  D3DLOCKED_RECT locked{};
  if (!check(texture->LockRect(0, &locked, nullptr, 0),
             "IDirect3DTexture9::LockRect(upload)"))
    return false;
  const auto q10 = [](std::uint8_t value) {
    return (static_cast<std::uint32_t>(value) * 1023U + 127U) / 255U;
  };
  const std::uint32_t packed10 =
      q10(color.r) | (q10(color.g) << 10U) | (q10(color.b) << 20U) |
      (3U << 30U);
  const std::uint16_t packed16f[4] = {
      float_to_half(static_cast<float>(color.r) / 255.0f),
      float_to_half(static_cast<float>(color.g) / 255.0f),
      float_to_half(static_cast<float>(color.b) / 255.0f), float_to_half(1.0f)};
  for (UINT y = 0; y < height; ++y) {
    auto *row = static_cast<std::uint8_t *>(locked.pBits) +
                static_cast<std::size_t>(y) * locked.Pitch;
    for (UINT x = 0; x < width; ++x) {
      if (format.dxgi_format == DXGI_FORMAT_R8G8B8A8_UNORM) {
        auto *pixel = row + static_cast<std::size_t>(x) * 4U;
        pixel[0] = color.r;
        pixel[1] = color.g;
        pixel[2] = color.b;
        pixel[3] = color.a;
      } else if (format.dxgi_format == DXGI_FORMAT_B8G8R8A8_UNORM) {
        auto *pixel = row + static_cast<std::size_t>(x) * 4U;
        pixel[0] = color.b;
        pixel[1] = color.g;
        pixel[2] = color.r;
        pixel[3] = color.a;
      } else if (format.dxgi_format == DXGI_FORMAT_R10G10B10A2_UNORM) {
        std::memcpy(row + static_cast<std::size_t>(x) * 4U, &packed10,
                    sizeof(packed10));
      } else {
        std::memcpy(row + static_cast<std::size_t>(x) * 8U, packed16f,
                    sizeof(packed16f));
      }
    }
  }
  return check(texture->UnlockRect(0), "IDirect3DTexture9::UnlockRect(upload)");
}

[[nodiscard]] UINT bytes_per_pixel(DXGI_FORMAT format) noexcept {
  return format == DXGI_FORMAT_R16G16B16A16_FLOAT ? 8U : 4U;
}

struct D3D11Side {
  ComPtr<ID3D11Device> device;
  ComPtr<ID3D11DeviceContext> context;
  ComPtr<IDXGIAdapter> adapter;
  DXGI_ADAPTER_DESC adapter_desc{};
};

[[nodiscard]] bool create_d3d11_side(D3D11Side &out) {
  D3D_FEATURE_LEVEL feature{};
  if (!check(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0,
                               nullptr, 0, D3D11_SDK_VERSION, &out.device,
                               &feature, &out.context),
             "D3D11CreateDevice"))
    return false;
  ComPtr<IDXGIDevice> dxgi_device;
  if (!check(out.device.As(&dxgi_device), "Query IDXGIDevice") ||
      !check(dxgi_device->GetAdapter(&out.adapter), "IDXGIDevice::GetAdapter") ||
      !check(out.adapter->GetDesc(&out.adapter_desc), "IDXGIAdapter::GetDesc"))
    return false;
  return true;
}

[[nodiscard]] bool validate_adapter(const D3DADAPTER_IDENTIFIER9 &d3d9,
                                    const DXGI_ADAPTER_DESC &d3d11) {
  const bool same = d3d9.VendorId == d3d11.VendorId &&
                    d3d9.DeviceId == d3d11.DeviceId;
  std::cout << "adapter_d3d9_vendor=0x" << std::hex << d3d9.VendorId
            << " device=0x" << d3d9.DeviceId << " adapter_d3d11_vendor=0x"
            << d3d11.VendorId << " device=0x" << d3d11.DeviceId << std::dec
            << " same_vendor_device=" << (same ? 1 : 0) << "\n";
  return same;
}

[[nodiscard]] bool validate_shared_texture(IDirect3DDevice9 *device9,
                                           IDirect3DTexture9 *texture9,
                                           HANDLE shared_handle,
                                           const FormatCase &format,
                                           D3D11Side &d3d11,
                                           GenerationSpec spec,
                                           std::uint32_t frame_base,
                                           std::uint64_t &mismatches,
                                           double &d3d9_sync_sum_ms,
                                           double &d3d11_readback_sum_ms) {
  ComPtr<ID3D11Texture2D> texture11;
  if (!check(d3d11.device->OpenSharedResource(
                 shared_handle, __uuidof(ID3D11Texture2D),
                 reinterpret_cast<void **>(texture11.GetAddressOf())),
             "ID3D11Device::OpenSharedResource"))
    return false;

  D3D11_TEXTURE2D_DESC desc{};
  texture11->GetDesc(&desc);
  std::cout << "format=" << format.name << " d3d11_bind_flags=0x" << std::hex
            << desc.BindFlags << std::dec << "\n";
  if (desc.Width != spec.width || desc.Height != spec.height ||
      desc.Format != format.dxgi_format || desc.MipLevels != 1 ||
      desc.ArraySize != 1 || desc.SampleDesc.Count != 1) {
    std::cerr << "D3D11 shared descriptor mismatch format=" << format.name
              << " actual_format=" << static_cast<unsigned>(desc.Format)
              << " size=" << desc.Width << "x" << desc.Height
              << " samples=" << desc.SampleDesc.Count << "\n";
    return false;
  }
  ComPtr<ID3D11ShaderResourceView> srv;
  ComPtr<ID3D11RenderTargetView> rtv;
  if (!check(d3d11.device->CreateShaderResourceView(texture11.Get(), nullptr,
                                                    &srv),
             "CreateShaderResourceView(shared)") ||
      !check(d3d11.device->CreateRenderTargetView(texture11.Get(), nullptr,
                                                  &rtv),
             "CreateRenderTargetView(shared)"))
    return false;

  D3D11_TEXTURE2D_DESC staging_desc = desc;
  staging_desc.Usage = D3D11_USAGE_STAGING;
  staging_desc.BindFlags = 0;
  staging_desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
  staging_desc.MiscFlags = 0;
  ComPtr<ID3D11Texture2D> staging;
  if (!check(d3d11.device->CreateTexture2D(&staging_desc, nullptr, &staging),
             "CreateTexture2D(staging)"))
    return false;

  ComPtr<IDirect3DTexture9> upload;
  if (!check(device9->CreateTexture(spec.width, spec.height, 1, 0,
                                    format.d3d9_format, D3DPOOL_SYSTEMMEM,
                                    &upload, nullptr),
             "IDirect3DDevice9::CreateTexture(upload)"))
    return false;

  const UINT bpp = bytes_per_pixel(format.dxgi_format);
  for (std::uint32_t local = 0; local < kFramesPerGeneration; ++local) {
    const std::uint32_t frame = frame_base + local;
    const auto expected = frame_color(frame);
    if (!fill_upload_texture(upload.Get(), format, expected, spec.width,
                             spec.height))
      return false;
    const auto d3d9_start = std::chrono::steady_clock::now();
    if (!check(device9->UpdateTexture(upload.Get(), texture9),
               "IDirect3DDevice9::UpdateTexture") ||
        !wait_d3d9_event(device9))
      return false;
    d3d9_sync_sum_ms += std::chrono::duration<double, std::milli>(
                            std::chrono::steady_clock::now() - d3d9_start)
                            .count();
    const auto d3d11_start = std::chrono::steady_clock::now();
    d3d11.context->CopyResource(staging.Get(), texture11.Get());
    D3D11_MAPPED_SUBRESOURCE mapped{};
    if (!check(d3d11.context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped),
               "ID3D11DeviceContext::Map"))
      return false;
    d3d11_readback_sum_ms += std::chrono::duration<double, std::milli>(
                                std::chrono::steady_clock::now() - d3d11_start)
                                .count();
    for (UINT y = 0; y < spec.height; ++y) {
      const auto *row = static_cast<const std::uint8_t *>(mapped.pData) +
                        static_cast<std::size_t>(y) * mapped.RowPitch;
      for (UINT x = 0; x < spec.width; ++x)
        if (!validate_pixel(format, row + static_cast<std::size_t>(x) * bpp,
                            expected))
          ++mismatches;
    }
    d3d11.context->Unmap(staging.Get(), 0);
  }
  return true;
}

struct RuntimeResult {
  bool created = false;
  bool adapter_match = false;
  std::uint32_t opened_formats = 0;
  std::uint32_t passed_formats = 0;
  std::uint64_t mismatches = 0;
};

template <typename DeviceFactory>
RuntimeResult run_runtime(const char *name, IDirect3D9 *d3d9,
                          DeviceFactory &&create_device, D3D11Side &d3d11) {
  RuntimeResult result{};
  D3DADAPTER_IDENTIFIER9 identifier{};
  if (!check(d3d9->GetAdapterIdentifier(D3DADAPTER_DEFAULT, 0, &identifier),
             "IDirect3D9::GetAdapterIdentifier"))
    return result;
  result.adapter_match = validate_adapter(identifier, d3d11.adapter_desc);
  if (!result.adapter_match)
    return result;

  ComPtr<IDirect3DDevice9> device;
  if (!create_device(device))
    return result;
  result.created = true;

  for (const auto &format : kFormats) {
    std::uint64_t case_mismatches = 0;
    double d3d9_sync_sum_ms = 0.0;
    double d3d11_readback_sum_ms = 0.0;
    bool complete = true;
    for (std::uint32_t generation = 0; generation < std::size(kGenerations);
         ++generation) {
      const auto spec = kGenerations[generation];
      HANDLE shared_handle = nullptr;
      ComPtr<IDirect3DTexture9> texture;
      const HRESULT create_hr = device->CreateTexture(
          spec.width, spec.height, 1, 0, format.d3d9_format, D3DPOOL_DEFAULT,
          &texture, &shared_handle);
      std::cout << "runtime=" << name << " format=" << format.name
                << " documented=" << (format.documented ? 1 : 0)
                << " generation=" << generation << " size=" << spec.width
                << "x" << spec.height << " create_hr=0x" << std::hex
                << static_cast<unsigned long>(create_hr) << std::dec
                << " shared_handle=" << (shared_handle ? 1 : 0) << "\n";
      if (FAILED(create_hr) || !shared_handle) {
        complete = false;
        break;
      }
      if (!validate_shared_texture(
              device.Get(), texture.Get(), shared_handle, format, d3d11, spec,
              generation * kFramesPerGeneration, case_mismatches,
              d3d9_sync_sum_ms, d3d11_readback_sum_ms)) {
        complete = false;
        break;
      }
    }
    if (!complete)
      continue;
    ++result.opened_formats;
    result.mismatches += case_mismatches;
    if (case_mismatches == 0)
      ++result.passed_formats;
    const auto total_frames =
        kFramesPerGeneration * static_cast<std::uint32_t>(std::size(kGenerations));
    std::cout << "runtime=" << name << " format=" << format.name
              << " frames=" << total_frames << " recreations="
              << (std::size(kGenerations) - 1U)
              << " mismatches=" << case_mismatches
              << " synchronization=d3d9_event_query_then_d3d11_readback "
              << std::fixed << std::setprecision(4)
              << "d3d9_update_event_cpu_wall_mean_ms="
              << (d3d9_sync_sum_ms / static_cast<double>(total_frames))
              << " d3d11_validation_copy_map_cpu_wall_mean_ms="
              << (d3d11_readback_sum_ms / static_cast<double>(total_frames))
              << " RESULT "
              << (case_mismatches == 0 ? "PASS" : "FAIL") << "\n";
  }
  return result;
}

} // namespace

int wmain() {
  const HWND hwnd = create_hidden_window();
  if (!hwnd) {
    std::cerr << "CreateWindowExW failed error=" << GetLastError() << "\n";
    return 2;
  }

  D3D11Side d3d11;
  if (!create_d3d11_side(d3d11)) {
    DestroyWindow(hwnd);
    return 3;
  }

  D3DPRESENT_PARAMETERS pp{};
  pp.Windowed = TRUE;
  pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
  pp.hDeviceWindow = hwnd;
  pp.BackBufferFormat = D3DFMT_UNKNOWN;
  pp.BackBufferWidth = 32;
  pp.BackBufferHeight = 32;
  pp.PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE;

  ComPtr<IDirect3D9> classic(Direct3DCreate9(D3D_SDK_VERSION));
  RuntimeResult classic_result{};
  if (classic) {
    classic_result = run_runtime(
        "classic", classic.Get(),
        [&](ComPtr<IDirect3DDevice9> &device) {
          HRESULT hr = classic->CreateDevice(
              D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, hwnd,
              D3DCREATE_HARDWARE_VERTEXPROCESSING | D3DCREATE_MULTITHREADED, &pp,
              &device);
          if (FAILED(hr))
            hr = classic->CreateDevice(
                D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, hwnd,
                D3DCREATE_SOFTWARE_VERTEXPROCESSING | D3DCREATE_MULTITHREADED,
                &pp, &device);
          return check(hr, "IDirect3D9::CreateDevice");
        },
        d3d11);
  }

  ComPtr<IDirect3D9Ex> ex;
  RuntimeResult ex_result{};
  const HRESULT ex_create = Direct3DCreate9Ex(D3D_SDK_VERSION, &ex);
  if (SUCCEEDED(ex_create) && ex) {
    ex_result = run_runtime(
        "ex", ex.Get(),
        [&](ComPtr<IDirect3DDevice9> &device) {
          ComPtr<IDirect3DDevice9Ex> device_ex;
          const HRESULT hr = ex->CreateDeviceEx(
              D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, hwnd,
              D3DCREATE_HARDWARE_VERTEXPROCESSING | D3DCREATE_MULTITHREADED, &pp,
              nullptr, &device_ex);
          if (SUCCEEDED(hr))
            return check(device_ex.As(&device), "Query IDirect3DDevice9");
          return check(hr, "IDirect3D9Ex::CreateDeviceEx");
        },
        d3d11);
  } else {
    std::cerr << "Direct3DCreate9Ex failed hr=0x" << std::hex
              << static_cast<unsigned long>(ex_create) << std::dec << "\n";
  }

  DestroyWindow(hwnd);
  const bool classic_pass = classic_result.created &&
                            classic_result.opened_formats > 0 &&
                            classic_result.opened_formats ==
                                classic_result.passed_formats;
  const bool ex_pass = ex_result.created && ex_result.opened_formats > 0 &&
                       ex_result.opened_formats == ex_result.passed_formats;
  std::cout << "summary classic_created=" << classic_result.created
            << " classic_opened_formats=" << classic_result.opened_formats
            << " classic_passed_formats=" << classic_result.passed_formats
            << " classic_mismatches=" << classic_result.mismatches
            << " ex_created=" << ex_result.created
            << " ex_opened_formats=" << ex_result.opened_formats
            << " ex_passed_formats=" << ex_result.passed_formats
            << " ex_mismatches=" << ex_result.mismatches << "\n";
  std::cout << "classic_runtime_result=" << (classic_pass ? "PASS" : "NO_PATH")
            << " ex_runtime_result=" << (ex_pass ? "PASS" : "NO_PATH")
            << "\nRESULT " << ((classic_pass || ex_pass) ? "PASS" : "FAIL")
            << "\n";
  return (classic_pass || ex_pass) ? 0 : 4;
}
