#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <d3d10_1.h>
#include <d3d11.h>
#include <dxgi.h>
#include <iomanip>
#include <iostream>
#include <limits>
#include <thread>
#include <windows.h>
#include <wrl/client.h>

using Microsoft::WRL::ComPtr;

namespace {

struct GenerationSpec {
  UINT width;
  UINT height;
};

constexpr GenerationSpec kGenerations[] = {{640, 360}, {1280, 720}};
constexpr std::uint32_t kFramesPerGeneration = 6;

struct Rgb {
  std::uint8_t r;
  std::uint8_t g;
  std::uint8_t b;
};

[[nodiscard]] bool check(HRESULT hr, const char *what) {
  if (SUCCEEDED(hr))
    return true;
  std::cerr << what << " failed hr=0x" << std::hex
            << static_cast<unsigned long>(hr) << std::dec << "\n";
  return false;
}

[[nodiscard]] Rgb frame_color(std::uint32_t frame) noexcept {
  return {static_cast<std::uint8_t>(31U + frame * 13U),
          static_cast<std::uint8_t>(71U + frame * 7U),
          static_cast<std::uint8_t>(113U + frame * 5U)};
}

[[nodiscard]] bool wait_d3d10_event(ID3D10Device *device) {
  D3D10_QUERY_DESC desc{};
  desc.Query = D3D10_QUERY_EVENT;
  ComPtr<ID3D10Query> query;
  if (!check(device->CreateQuery(&desc, &query), "CreateQuery(D3D10 event)"))
    return false;
  query->End();
  const auto deadline =
      std::chrono::steady_clock::now() + std::chrono::seconds(5);
  while (std::chrono::steady_clock::now() < deadline) {
    const HRESULT hr = query->GetData(nullptr, 0, 0);
    if (hr == S_OK)
      return true;
    if (FAILED(hr))
      return check(hr, "GetData(D3D10 event)");
    std::this_thread::yield();
  }
  std::cerr << "GetData(D3D10 event) timed out\n";
  return false;
}

[[nodiscard]] std::uint8_t r10_to_u8(std::uint32_t value) noexcept {
  return static_cast<std::uint8_t>((value * 255U + 511U) / 1023U);
}

[[nodiscard]] Rgb read_r10(const std::uint8_t *pixel) noexcept {
  std::uint32_t packed = 0;
  std::memcpy(&packed, pixel, sizeof(packed));
  return {r10_to_u8(packed & 0x3FFU),
          r10_to_u8((packed >> 10U) & 0x3FFU),
          r10_to_u8((packed >> 20U) & 0x3FFU)};
}

[[nodiscard]] bool near_color(Rgb actual, Rgb expected,
                              int tolerance = 1) noexcept {
  const auto close = [=](std::uint8_t a, std::uint8_t e) {
    const int delta = static_cast<int>(a) - static_cast<int>(e);
    return delta >= -tolerance && delta <= tolerance;
  };
  return close(actual.r, expected.r) && close(actual.g, expected.g) &&
         close(actual.b, expected.b);
}

struct Devices {
  ComPtr<ID3D10Device1> d3d10;
  ComPtr<ID3D11Device> d3d11;
  ComPtr<ID3D11DeviceContext> context11;
  DXGI_ADAPTER_DESC adapter10{};
  DXGI_ADAPTER_DESC adapter11{};
};

[[nodiscard]] bool query_adapter(IUnknown *device, DXGI_ADAPTER_DESC &desc) {
  ComPtr<IDXGIDevice> dxgi_device;
  ComPtr<IDXGIAdapter> adapter;
  return check(device->QueryInterface(IID_PPV_ARGS(&dxgi_device)),
               "Query IDXGIDevice") &&
         check(dxgi_device->GetAdapter(&adapter), "IDXGIDevice::GetAdapter") &&
         check(adapter->GetDesc(&desc), "IDXGIAdapter::GetDesc");
}

[[nodiscard]] bool create_devices(Devices &out) {
  if (!check(D3D10CreateDevice1(nullptr, D3D10_DRIVER_TYPE_HARDWARE, nullptr,
                                D3D10_CREATE_DEVICE_BGRA_SUPPORT,
                                D3D10_FEATURE_LEVEL_10_0, D3D10_1_SDK_VERSION,
                                &out.d3d10),
             "D3D10CreateDevice1"))
    return false;
  D3D_FEATURE_LEVEL feature{};
  if (!check(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
                               D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0,
                               D3D11_SDK_VERSION, &out.d3d11, &feature,
                               &out.context11),
             "D3D11CreateDevice"))
    return false;
  if (!query_adapter(out.d3d10.Get(), out.adapter10) ||
      !query_adapter(out.d3d11.Get(), out.adapter11))
    return false;
  const bool same =
      out.adapter10.AdapterLuid.LowPart == out.adapter11.AdapterLuid.LowPart &&
      out.adapter10.AdapterLuid.HighPart == out.adapter11.AdapterLuid.HighPart;
  std::cout << "d3d10_d3d11_adapter_luid_match=" << (same ? 1 : 0)
            << " vendor=0x" << std::hex << out.adapter10.VendorId
            << " device=0x" << out.adapter10.DeviceId << std::dec << "\n";
  return same;
}

struct RelayResources {
  ComPtr<ID3D10Texture2D> engine_color;
  ComPtr<ID3D10RenderTargetView> engine_rtv;
  ComPtr<ID3D10Texture2D> relay10;
  HANDLE shared_handle = nullptr;
  ComPtr<ID3D11Texture2D> relay11;
  ComPtr<ID3D11Texture2D> staging11;
};

[[nodiscard]] bool create_relay(Devices &devices, GenerationSpec spec,
                                RelayResources &out) {
  D3D10_TEXTURE2D_DESC desc{};
  desc.Width = spec.width;
  desc.Height = spec.height;
  desc.MipLevels = 1;
  desc.ArraySize = 1;
  desc.Format = DXGI_FORMAT_R10G10B10A2_UNORM;
  desc.SampleDesc.Count = 1;
  desc.Usage = D3D10_USAGE_DEFAULT;
  desc.BindFlags = D3D10_BIND_RENDER_TARGET | D3D10_BIND_SHADER_RESOURCE;
  if (!check(devices.d3d10->CreateTexture2D(&desc, nullptr, &out.engine_color),
             "CreateTexture2D(D3D10 engine-color)") ||
      !check(devices.d3d10->CreateRenderTargetView(out.engine_color.Get(),
                                                   nullptr, &out.engine_rtv),
             "CreateRenderTargetView(D3D10 engine-color)"))
    return false;

  desc.MiscFlags = D3D10_RESOURCE_MISC_SHARED;
  if (!check(devices.d3d10->CreateTexture2D(&desc, nullptr, &out.relay10),
             "CreateTexture2D(D3D10 shared-relay)"))
    return false;
  ComPtr<IDXGIResource> dxgi_resource;
  if (!check(out.relay10.As(&dxgi_resource), "Query IDXGIResource(relay)") ||
      !check(dxgi_resource->GetSharedHandle(&out.shared_handle),
             "IDXGIResource::GetSharedHandle") ||
      !out.shared_handle ||
      !check(devices.d3d11->OpenSharedResource(
                 out.shared_handle, __uuidof(ID3D11Texture2D),
                 reinterpret_cast<void **>(out.relay11.GetAddressOf())),
             "ID3D11Device::OpenSharedResource(D3D10 relay)"))
    return false;

  D3D11_TEXTURE2D_DESC desc11{};
  out.relay11->GetDesc(&desc11);
  if (desc11.Width != spec.width || desc11.Height != spec.height ||
      desc11.Format != DXGI_FORMAT_R10G10B10A2_UNORM ||
      (desc11.BindFlags &
       (D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE)) !=
          (D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE)) {
    std::cerr << "reject=d3d10_relay_descriptor size=" << desc11.Width << "x"
              << desc11.Height
              << " format=" << static_cast<unsigned>(desc11.Format)
              << " bind_flags=0x" << std::hex << desc11.BindFlags << std::dec
              << "\n";
    return false;
  }
  D3D11_TEXTURE2D_DESC staging_desc = desc11;
  staging_desc.Usage = D3D11_USAGE_STAGING;
  staging_desc.BindFlags = 0;
  staging_desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
  staging_desc.MiscFlags = 0;
  return check(devices.d3d11->CreateTexture2D(&staging_desc, nullptr,
                                              &out.staging11),
               "CreateTexture2D(D3D11 relay-staging)");
}

struct EventResult {
  std::uint64_t mismatches = 0;
  double copy_event_wall_ms = 0.0;
};

[[nodiscard]] bool run_event_generation(Devices &devices, RelayResources &relay,
                                        GenerationSpec spec,
                                        std::uint32_t frame_base,
                                        EventResult &result) {
  for (std::uint32_t local = 0; local < kFramesPerGeneration; ++local) {
    const std::uint32_t frame = frame_base + local;
    const auto expected = frame_color(frame);
    const float clear[4] = {static_cast<float>(expected.r) / 255.0f,
                            static_cast<float>(expected.g) / 255.0f,
                            static_cast<float>(expected.b) / 255.0f, 1.0f};
    devices.d3d10->ClearRenderTargetView(relay.engine_rtv.Get(), clear);
    const auto start = std::chrono::steady_clock::now();
    devices.d3d10->CopyResource(relay.relay10.Get(), relay.engine_color.Get());
    if (!wait_d3d10_event(devices.d3d10.Get()))
      return false;
    result.copy_event_wall_ms += std::chrono::duration<double, std::milli>(
                                     std::chrono::steady_clock::now() - start)
                                     .count();

    devices.context11->CopyResource(relay.staging11.Get(), relay.relay11.Get());
    D3D11_MAPPED_SUBRESOURCE mapped{};
    if (!check(devices.context11->Map(relay.staging11.Get(), 0, D3D11_MAP_READ,
                                      0, &mapped),
               "Map(D3D10 relay staging)"))
      return false;
    for (UINT y = 0; y < spec.height; ++y) {
      const auto *row = static_cast<const std::uint8_t *>(mapped.pData) +
                        static_cast<std::size_t>(y) * mapped.RowPitch;
      for (UINT x = 0; x < spec.width; ++x)
        if (!near_color(read_r10(row + static_cast<std::size_t>(x) * 4U),
                        expected))
          ++result.mismatches;
    }
    devices.context11->Unmap(relay.staging11.Get(), 0);
  }
  return true;
}

struct KeyedResult {
  bool created = false;
  bool mutex10 = false;
  bool opened11 = false;
  bool mutex11 = false;
  bool roundtrip = false;
  HRESULT create_hr = E_FAIL;
};

[[nodiscard]] KeyedResult probe_keyed_mutex(Devices &devices) {
  KeyedResult result{};
  D3D10_TEXTURE2D_DESC desc{};
  desc.Width = 64;
  desc.Height = 64;
  desc.MipLevels = 1;
  desc.ArraySize = 1;
  desc.Format = DXGI_FORMAT_R10G10B10A2_UNORM;
  desc.SampleDesc.Count = 1;
  desc.Usage = D3D10_USAGE_DEFAULT;
  desc.BindFlags = D3D10_BIND_RENDER_TARGET | D3D10_BIND_SHADER_RESOURCE;
  desc.MiscFlags = D3D10_RESOURCE_MISC_SHARED_KEYEDMUTEX;
  ComPtr<ID3D10Texture2D> texture10;
  result.create_hr = devices.d3d10->CreateTexture2D(&desc, nullptr, &texture10);
  if (FAILED(result.create_hr))
    return result;
  result.created = true;
  ComPtr<IDXGIResource> dxgi_resource;
  ComPtr<IDXGIKeyedMutex> mutex10;
  HANDLE handle = nullptr;
  if (FAILED(texture10.As(&dxgi_resource)) ||
      FAILED(texture10.As(&mutex10)) ||
      FAILED(dxgi_resource->GetSharedHandle(&handle)) || !handle)
    return result;
  result.mutex10 = true;
  ComPtr<ID3D11Texture2D> texture11;
  if (FAILED(devices.d3d11->OpenSharedResource(handle,
                                               IID_PPV_ARGS(&texture11))))
    return result;
  result.opened11 = true;
  ComPtr<IDXGIKeyedMutex> mutex11;
  if (FAILED(texture11.As(&mutex11)))
    return result;
  result.mutex11 = true;

  ComPtr<ID3D10RenderTargetView> rtv10;
  if (FAILED(devices.d3d10->CreateRenderTargetView(texture10.Get(), nullptr,
                                                   &rtv10)))
    return result;
  D3D11_TEXTURE2D_DESC desc11{};
  texture11->GetDesc(&desc11);
  desc11.Usage = D3D11_USAGE_STAGING;
  desc11.BindFlags = 0;
  desc11.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
  desc11.MiscFlags = 0;
  ComPtr<ID3D11Texture2D> staging;
  if (FAILED(devices.d3d11->CreateTexture2D(&desc11, nullptr, &staging)))
    return result;

  if (FAILED(mutex10->AcquireSync(0, 1000)))
    return result;
  const Rgb expected{91, 137, 203};
  const float clear[4] = {expected.r / 255.0f, expected.g / 255.0f,
                          expected.b / 255.0f, 1.0f};
  devices.d3d10->ClearRenderTargetView(rtv10.Get(), clear);
  if (FAILED(mutex10->ReleaseSync(1)))
    return result;
  if (FAILED(mutex11->AcquireSync(1, 1000)))
    return result;
  devices.context11->CopyResource(staging.Get(), texture11.Get());
  D3D11_MAPPED_SUBRESOURCE mapped{};
  const HRESULT map_hr =
      devices.context11->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped);
  if (SUCCEEDED(map_hr)) {
    result.roundtrip = near_color(read_r10(
                                     static_cast<const std::uint8_t *>(
                                         mapped.pData)),
                                 expected);
    devices.context11->Unmap(staging.Get(), 0);
  }
  mutex11->ReleaseSync(0);
  return result;
}

} // namespace

int wmain() {
  static_assert(sizeof(void *) == 4, "D3D10 relay probe must be built x86");
  Devices devices;
  if (!create_devices(devices))
    return 2;

  EventResult event{};
  for (std::uint32_t generation = 0; generation < std::size(kGenerations);
       ++generation) {
    RelayResources relay;
    if (!create_relay(devices, kGenerations[generation], relay) ||
        !run_event_generation(
            devices, relay, kGenerations[generation],
            generation * kFramesPerGeneration, event))
      return 3;
  }
  const auto total_frames =
      kFramesPerGeneration * static_cast<std::uint32_t>(std::size(kGenerations));
  std::cout << std::fixed << std::setprecision(4)
            << "event_relay_frames=" << total_frames
            << " generations=" << std::size(kGenerations)
            << " generation0=" << kGenerations[0].width << "x"
            << kGenerations[0].height << " generation1=" << kGenerations[1].width
            << "x" << kGenerations[1].height
            << " resource_recreations=1 mismatches=" << event.mismatches
            << " d3d10_copy_event_cpu_wall_mean_ms="
            << (event.copy_event_wall_ms / static_cast<double>(total_frames))
            << "\n";

  const KeyedResult keyed = probe_keyed_mutex(devices);
  std::cout << "keyed_mutex_create_hr=0x" << std::hex
            << static_cast<unsigned long>(keyed.create_hr) << std::dec
            << " created=" << keyed.created << " mutex10=" << keyed.mutex10
            << " opened11=" << keyed.opened11 << " mutex11=" << keyed.mutex11
            << " roundtrip=" << keyed.roundtrip << "\n";

  const bool event_pass = event.mismatches == 0;
  const bool keyed_consistent =
      !keyed.created ||
      (keyed.mutex10 && keyed.opened11 && keyed.mutex11 && keyed.roundtrip);
  const bool pass = event_pass && keyed_consistent;
  std::cout << "event_query_result=" << (event_pass ? "PASS" : "FAIL")
            << " keyed_mutex_result="
            << (!keyed.created ? "UNAVAILABLE" : (keyed.roundtrip ? "PASS" : "FAIL"))
            << "\nRESULT " << (pass ? "PASS" : "FAIL") << "\n";
  return pass ? 0 : 4;
}
