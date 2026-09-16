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

struct Vertex {
  float x;
  float y;
  float z;
  D3DCOLOR color;
};

constexpr DWORD kFvf = D3DFVF_XYZ | D3DFVF_DIFFUSE;

constexpr Vertex kSceneVertices[] = {
    {-0.70f, -0.60f, 0.40f, D3DCOLOR_XRGB(220, 40, 40)},
    {-0.20f, 0.60f, 0.40f, D3DCOLOR_XRGB(220, 40, 40)},
    {0.30f, -0.60f, 0.40f, D3DCOLOR_XRGB(220, 40, 40)},
    {-0.30f, -0.60f, 0.80f, D3DCOLOR_XRGB(40, 220, 40)},
    {0.20f, 0.60f, 0.80f, D3DCOLOR_XRGB(40, 220, 40)},
    {0.70f, -0.60f, 0.80f, D3DCOLOR_XRGB(40, 220, 40)},
};

[[nodiscard]] bool check(HRESULT hr, const char *what) {
  if (SUCCEEDED(hr))
    return true;
  std::cerr << what << " failed hr=0x" << std::hex
            << static_cast<unsigned long>(hr) << std::dec << "\n";
  return false;
}

[[nodiscard]] HWND create_hidden_window() {
  const wchar_t *class_name = L"LTRD3D9ExSceneProbeWindow";
  WNDCLASSEXW wc{};
  wc.cbSize = sizeof(wc);
  wc.lpfnWndProc = DefWindowProcW;
  wc.hInstance = GetModuleHandleW(nullptr);
  wc.lpszClassName = class_name;
  if (!RegisterClassExW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
    return nullptr;
  return CreateWindowExW(0, class_name, L"LTR D3D9Ex scene probe",
                         WS_OVERLAPPED, 0, 0, 64, 64, nullptr, nullptr,
                         wc.hInstance, nullptr);
}

[[nodiscard]] bool wait_d3d9_event(IDirect3DDevice9 *device) {
  ComPtr<IDirect3DQuery9> query;
  if (!check(device->CreateQuery(D3DQUERYTYPE_EVENT, &query),
             "CreateQuery(scene-event)") ||
      !check(query->Issue(D3DISSUE_END), "Issue(scene-event)"))
    return false;
  const auto deadline =
      std::chrono::steady_clock::now() + std::chrono::seconds(5);
  while (std::chrono::steady_clock::now() < deadline) {
    const HRESULT hr = query->GetData(nullptr, 0, D3DGETDATA_FLUSH);
    if (hr == S_OK)
      return true;
    if (FAILED(hr))
      return check(hr, "GetData(scene-event)");
    std::this_thread::yield();
  }
  std::cerr << "GetData(scene-event) timed out\n";
  return false;
}

[[nodiscard]] D3DMATRIX identity_matrix() noexcept {
  D3DMATRIX m{};
  m._11 = 1.0f;
  m._22 = 1.0f;
  m._33 = 1.0f;
  m._44 = 1.0f;
  return m;
}

[[nodiscard]] D3DMATRIX world_matrix(std::uint32_t frame) noexcept {
  D3DMATRIX m = identity_matrix();
  m._41 = (frame & 1U) ? 0.03f : -0.03f;
  return m;
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
  return check(out.device.As(&dxgi_device), "Query IDXGIDevice") &&
         check(dxgi_device->GetAdapter(&out.adapter),
               "IDXGIDevice::GetAdapter") &&
         check(out.adapter->GetDesc(&out.adapter_desc),
               "IDXGIAdapter::GetDesc");
}

struct SceneResources {
  ComPtr<IDirect3DTexture9> color;
  ComPtr<IDirect3DSurface9> color_surface;
  ComPtr<IDirect3DSurface9> depth;
  ComPtr<IDirect3DVertexBuffer9> vertices;
  ComPtr<IDirect3DTexture9> relay;
  ComPtr<IDirect3DSurface9> relay_surface;
  HANDLE relay_handle = nullptr;
  ComPtr<ID3D11Texture2D> relay11;
  ComPtr<ID3D11Texture2D> staging11;
};

[[nodiscard]] bool create_scene_resources(IDirect3DDevice9Ex *device9,
                                          D3D11Side &d3d11,
                                          GenerationSpec spec,
                                          SceneResources &out) {
  if (!check(device9->CreateTexture(spec.width, spec.height, 1,
                                    D3DUSAGE_RENDERTARGET,
                                    D3DFMT_A2B10G10R10, D3DPOOL_DEFAULT,
                                    &out.color, nullptr),
             "CreateTexture(engine-color)") ||
      !check(out.color->GetSurfaceLevel(0, &out.color_surface),
             "GetSurfaceLevel(engine-color)") ||
      !check(device9->CreateDepthStencilSurface(
                 spec.width, spec.height, D3DFMT_D24S8, D3DMULTISAMPLE_NONE, 0,
                 TRUE, &out.depth, nullptr),
             "CreateDepthStencilSurface(engine-depth)") ||
      !check(device9->CreateVertexBuffer(sizeof(kSceneVertices), 0, kFvf,
                                         D3DPOOL_DEFAULT, &out.vertices,
                                         nullptr),
             "CreateVertexBuffer(engine-geometry)"))
    return false;

  void *mapped = nullptr;
  if (!check(out.vertices->Lock(0, sizeof(kSceneVertices), &mapped, 0),
             "Lock(engine-geometry)"))
    return false;
  std::memcpy(mapped, kSceneVertices, sizeof(kSceneVertices));
  if (!check(out.vertices->Unlock(), "Unlock(engine-geometry)"))
    return false;

  out.relay_handle = nullptr;
  if (!check(device9->CreateTexture(spec.width, spec.height, 1, 0,
                                    D3DFMT_A2B10G10R10, D3DPOOL_DEFAULT,
                                    &out.relay, &out.relay_handle),
             "CreateTexture(shared-relay)") ||
      !out.relay_handle ||
      !check(out.relay->GetSurfaceLevel(0, &out.relay_surface),
             "GetSurfaceLevel(shared-relay)") ||
      !check(d3d11.device->OpenSharedResource(
                 out.relay_handle, __uuidof(ID3D11Texture2D),
                 reinterpret_cast<void **>(out.relay11.GetAddressOf())),
             "OpenSharedResource(scene-relay)"))
    return false;

  D3D11_TEXTURE2D_DESC relay_desc{};
  out.relay11->GetDesc(&relay_desc);
  if (relay_desc.Width != spec.width || relay_desc.Height != spec.height ||
      relay_desc.Format != DXGI_FORMAT_R10G10B10A2_UNORM ||
      (relay_desc.BindFlags &
       (D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET)) !=
          (D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET)) {
    std::cerr << "reject=scene_relay_descriptor size=" << relay_desc.Width << "x"
              << relay_desc.Height
              << " format=" << static_cast<unsigned>(relay_desc.Format)
              << " bind_flags=0x" << std::hex << relay_desc.BindFlags << std::dec
              << "\n";
    return false;
  }
  D3D11_TEXTURE2D_DESC staging_desc = relay_desc;
  staging_desc.Usage = D3D11_USAGE_STAGING;
  staging_desc.BindFlags = 0;
  staging_desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
  staging_desc.MiscFlags = 0;
  return check(d3d11.device->CreateTexture2D(&staging_desc, nullptr,
                                             &out.staging11),
               "CreateTexture2D(scene-staging)");
}

[[nodiscard]] std::uint8_t r10_to_u8(std::uint32_t value) noexcept {
  return static_cast<std::uint8_t>((value * 255U + 511U) / 1023U);
}

struct Rgb {
  std::uint8_t r;
  std::uint8_t g;
  std::uint8_t b;
};

[[nodiscard]] Rgb read_r10(const std::uint8_t *pixel) noexcept {
  std::uint32_t packed = 0;
  std::memcpy(&packed, pixel, sizeof(packed));
  return {r10_to_u8(packed & 0x3FFU),
          r10_to_u8((packed >> 10U) & 0x3FFU),
          r10_to_u8((packed >> 20U) & 0x3FFU)};
}

[[nodiscard]] bool near_color(Rgb actual, Rgb expected,
                              int tolerance = 2) noexcept {
  const auto close = [=](std::uint8_t a, std::uint8_t e) {
    return std::abs(static_cast<int>(a) - static_cast<int>(e)) <= tolerance;
  };
  return close(actual.r, expected.r) && close(actual.g, expected.g) &&
         close(actual.b, expected.b);
}

[[nodiscard]] const std::uint8_t *pixel_at(const D3D11_MAPPED_SUBRESOURCE &map,
                                           GenerationSpec spec, float ndc_x,
                                           float ndc_y) noexcept {
  const auto fx = (ndc_x * 0.5f + 0.5f) * static_cast<float>(spec.width);
  const auto fy = (-ndc_y * 0.5f + 0.5f) * static_cast<float>(spec.height);
  const UINT x = std::min(spec.width - 1U,
                          static_cast<UINT>(std::max(0.0f, fx)));
  const UINT y = std::min(spec.height - 1U,
                          static_cast<UINT>(std::max(0.0f, fy)));
  return static_cast<const std::uint8_t *>(map.pData) +
         static_cast<std::size_t>(y) * map.RowPitch +
         static_cast<std::size_t>(x) * 4U;
}

[[nodiscard]] bool render_and_capture_frame(
    IDirect3DDevice9Ex *device9, D3D11Side &d3d11, SceneResources &resources,
    GenerationSpec spec, std::uint32_t frame, std::uint64_t &mismatches,
    double &capture_wall_ms, bool &bound_rt_observed, bool &depth_observed,
    bool &transform_observed) {
  const auto world = world_matrix(frame);
  const auto identity = identity_matrix();
  if (!check(device9->SetRenderTarget(0, resources.color_surface.Get()),
             "SetRenderTarget(engine-color)") ||
      !check(device9->SetDepthStencilSurface(resources.depth.Get()),
             "SetDepthStencilSurface(engine-depth)") ||
      !check(device9->SetTransform(D3DTS_WORLD, &world), "SetTransform(world)") ||
      !check(device9->SetTransform(D3DTS_VIEW, &identity), "SetTransform(view)") ||
      !check(device9->SetTransform(D3DTS_PROJECTION, &identity),
             "SetTransform(projection)") ||
      !check(device9->SetRenderState(D3DRS_LIGHTING, FALSE),
             "SetRenderState(lighting)") ||
      !check(device9->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE),
             "SetRenderState(cull)") ||
      !check(device9->SetRenderState(D3DRS_ZENABLE, D3DZB_TRUE),
             "SetRenderState(zenable)") ||
      !check(device9->SetRenderState(D3DRS_ZWRITEENABLE, TRUE),
             "SetRenderState(zwrite)") ||
      !check(device9->SetFVF(kFvf), "SetFVF(scene)") ||
      !check(device9->SetStreamSource(0, resources.vertices.Get(), 0,
                                      sizeof(Vertex)),
             "SetStreamSource(scene)"))
    return false;

  D3DVIEWPORT9 viewport{0, 0, spec.width, spec.height, 0.0f, 1.0f};
  if (!check(device9->SetViewport(&viewport), "SetViewport(scene)") ||
      !check(device9->Clear(0, nullptr, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER,
                            D3DCOLOR_XRGB(20, 40, 120), 1.0f, 0),
             "Clear(scene)") ||
      !check(device9->BeginScene(), "BeginScene") ||
      !check(device9->DrawPrimitive(D3DPT_TRIANGLELIST, 0, 1),
             "DrawPrimitive(red-near)") ||
      !check(device9->DrawPrimitive(D3DPT_TRIANGLELIST, 3, 1),
             "DrawPrimitive(green-far)") ||
      !check(device9->EndScene(), "EndScene"))
    return false;

  ComPtr<IDirect3DSurface9> current_rt, current_depth;
  D3DMATRIX observed_world{};
  if (!check(device9->GetRenderTarget(0, &current_rt),
             "GetRenderTarget(capture)") ||
      !check(device9->GetDepthStencilSurface(&current_depth),
             "GetDepthStencilSurface(capture)") ||
      !check(device9->GetTransform(D3DTS_WORLD, &observed_world),
             "GetTransform(capture-world)"))
    return false;
  bound_rt_observed = current_rt.Get() == resources.color_surface.Get();
  depth_observed = current_depth.Get() == resources.depth.Get();
  transform_observed = std::abs(observed_world._41 - world._41) < 0.0001f;
  if (!bound_rt_observed || !depth_observed || !transform_observed) {
    std::cerr << "reject=scene_state_visibility bound_rt=" << bound_rt_observed
              << " depth=" << depth_observed
              << " transform=" << transform_observed << "\n";
    return false;
  }

  const auto capture_start = std::chrono::steady_clock::now();
  if (!check(device9->StretchRect(current_rt.Get(), nullptr,
                                  resources.relay_surface.Get(), nullptr,
                                  D3DTEXF_NONE),
             "StretchRect(bound-engine-rt->relay)") ||
      !wait_d3d9_event(device9))
    return false;
  capture_wall_ms += std::chrono::duration<double, std::milli>(
                         std::chrono::steady_clock::now() - capture_start)
                         .count();

  d3d11.context->CopyResource(resources.staging11.Get(), resources.relay11.Get());
  D3D11_MAPPED_SUBRESOURCE mapped{};
  if (!check(d3d11.context->Map(resources.staging11.Get(), 0, D3D11_MAP_READ, 0,
                                &mapped),
             "Map(scene-staging)"))
    return false;

  const Rgb clear_expected{20, 40, 120};
  const Rgb red_expected{220, 40, 40};
  const Rgb green_expected{40, 220, 40};
  const auto clear_pixel = read_r10(pixel_at(mapped, spec, -0.85f, 0.80f));
  const auto red_pixel = read_r10(pixel_at(mapped, spec, -0.25f, 0.0f));
  const auto overlap_pixel = read_r10(pixel_at(mapped, spec, 0.0f, 0.0f));
  const auto green_pixel = read_r10(pixel_at(mapped, spec, 0.25f, 0.0f));
  if (!near_color(clear_pixel, clear_expected))
    ++mismatches;
  if (!near_color(red_pixel, red_expected))
    ++mismatches;
  if (!near_color(overlap_pixel, red_expected))
    ++mismatches;
  if (!near_color(green_pixel, green_expected))
    ++mismatches;
  d3d11.context->Unmap(resources.staging11.Get(), 0);
  return true;
}

} // namespace

int wmain() {
  static_assert(sizeof(void *) == 4, "scene probe must be built x86");
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

  ComPtr<IDirect3D9Ex> d3d9;
  if (!check(Direct3DCreate9Ex(D3D_SDK_VERSION, &d3d9),
             "Direct3DCreate9Ex")) {
    DestroyWindow(hwnd);
    return 4;
  }
  LUID d3d9_luid{};
  if (!check(d3d9->GetAdapterLUID(D3DADAPTER_DEFAULT, &d3d9_luid),
             "IDirect3D9Ex::GetAdapterLUID")) {
    DestroyWindow(hwnd);
    return 4;
  }
  const bool adapter_match =
      d3d9_luid.LowPart == d3d11.adapter_desc.AdapterLuid.LowPart &&
      d3d9_luid.HighPart == d3d11.adapter_desc.AdapterLuid.HighPart;
  std::cout << "scene_adapter_luid_match=" << (adapter_match ? 1 : 0) << "\n";
  if (!adapter_match) {
    DestroyWindow(hwnd);
    return 5;
  }

  D3DPRESENT_PARAMETERS pp{};
  pp.Windowed = TRUE;
  pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
  pp.hDeviceWindow = hwnd;
  pp.BackBufferFormat = D3DFMT_UNKNOWN;
  pp.BackBufferWidth = 64;
  pp.BackBufferHeight = 64;
  pp.PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE;
  ComPtr<IDirect3DDevice9Ex> device9;
  HRESULT create_hr = d3d9->CreateDeviceEx(
      D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, hwnd,
      D3DCREATE_HARDWARE_VERTEXPROCESSING | D3DCREATE_MULTITHREADED, &pp,
      nullptr, &device9);
  if (FAILED(create_hr))
    create_hr = d3d9->CreateDeviceEx(
        D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, hwnd,
        D3DCREATE_SOFTWARE_VERTEXPROCESSING | D3DCREATE_MULTITHREADED, &pp,
        nullptr, &device9);
  if (!check(create_hr, "IDirect3D9Ex::CreateDeviceEx")) {
    DestroyWindow(hwnd);
    return 6;
  }

  ComPtr<IDirect3DSurface9> default_rt;
  if (!check(device9->GetRenderTarget(0, &default_rt),
             "GetRenderTarget(default)")) {
    DestroyWindow(hwnd);
    return 7;
  }

  std::uint64_t mismatches = 0;
  std::uint32_t frame = 0;
  std::uint32_t resets = 0;
  double capture_wall_ms = 0.0;
  bool bound_rt_observed = false;
  bool depth_observed = false;
  bool transform_observed = false;

  for (std::uint32_t generation = 0; generation < std::size(kGenerations);
       ++generation) {
    SceneResources resources;
    if (!create_scene_resources(device9.Get(), d3d11, kGenerations[generation],
                                resources)) {
      DestroyWindow(hwnd);
      return 8;
    }
    for (std::uint32_t local = 0; local < kFramesPerGeneration;
         ++local, ++frame) {
      if (!render_and_capture_frame(
              device9.Get(), d3d11, resources, kGenerations[generation], frame,
              mismatches, capture_wall_ms, bound_rt_observed, depth_observed,
              transform_observed)) {
        DestroyWindow(hwnd);
        return 9;
      }
    }
    if (!check(device9->SetRenderTarget(0, default_rt.Get()),
               "SetRenderTarget(default-before-reset)") ||
        !check(device9->SetDepthStencilSurface(nullptr),
               "SetDepthStencilSurface(null-before-reset)")) {
      DestroyWindow(hwnd);
      return 10;
    }
    resources = {};
    if (generation + 1U < std::size(kGenerations)) {
      default_rt.Reset();
      pp.BackBufferWidth += 16U;
      pp.BackBufferHeight += 16U;
      const HRESULT reset_hr = device9->ResetEx(&pp, nullptr);
      std::cout << "scene_reset_hr=0x" << std::hex
                << static_cast<unsigned long>(reset_hr) << std::dec
                << " backbuffer=" << pp.BackBufferWidth << "x"
                << pp.BackBufferHeight << "\n";
      if (!check(reset_hr, "IDirect3DDevice9Ex::ResetEx")) {
        DestroyWindow(hwnd);
        return 10;
      }
      ++resets;
      if (!check(device9->GetRenderTarget(0, &default_rt),
                 "GetRenderTarget(default-after-reset)")) {
        DestroyWindow(hwnd);
        return 10;
      }
    }
  }

  const auto total_frames =
      kFramesPerGeneration * static_cast<std::uint32_t>(std::size(kGenerations));
  std::cout << std::fixed << std::setprecision(4)
            << "scene_frames=" << total_frames
            << " generations=" << std::size(kGenerations)
            << " generation0=" << kGenerations[0].width << "x"
            << kGenerations[0].height << " generation1=" << kGenerations[1].width
            << "x" << kGenerations[1].height << " resets=" << resets
            << " mismatches=" << mismatches
            << " engine_rt_bound_during_capture=" << bound_rt_observed
            << " engine_depth_bound_during_capture=" << depth_observed
            << " world_transform_visible=" << transform_observed
            << " depth_occlusion_validated=1"
            << " d3d9ex_bound_rt_stretchrect_event_cpu_wall_mean_ms="
            << (capture_wall_ms / static_cast<double>(total_frames)) << "\n";
  const bool pass = mismatches == 0 && resets == 1 && bound_rt_observed &&
                    depth_observed && transform_observed;
  std::cout << "RESULT " << (pass ? "PASS" : "FAIL") << "\n";
  DestroyWindow(hwnd);
  return pass ? 0 : 11;
}
