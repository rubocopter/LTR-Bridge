#include <cstdint>
#include <cstring>
#include <cwchar>
#include <d3d9.h>
#include <iostream>
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
  const wchar_t *class_name = L"LTRD3D9ExInterceptTargetWindow";
  WNDCLASSEXW wc{};
  wc.cbSize = sizeof(wc);
  wc.lpfnWndProc = DefWindowProcW;
  wc.hInstance = GetModuleHandleW(nullptr);
  wc.lpszClassName = class_name;
  if (!RegisterClassExW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
    return nullptr;
  return CreateWindowExW(0, class_name, L"LTR external D3D9Ex target",
                         WS_OVERLAPPED, 0, 0, 64, 64, nullptr, nullptr,
                         wc.hInstance, nullptr);
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

struct SceneResources {
  ComPtr<IDirect3DTexture9> color;
  ComPtr<IDirect3DSurface9> color_surface;
  ComPtr<IDirect3DSurface9> depth;
  ComPtr<IDirect3DVertexBuffer9> vertices;
};

[[nodiscard]] bool create_scene_resources(IDirect3DDevice9Ex *device,
                                          GenerationSpec spec,
                                          D3DFORMAT color_format,
                                          SceneResources &out) {
  if (!check(device->CreateTexture(spec.width, spec.height, 1,
                                   D3DUSAGE_RENDERTARGET,
                                   color_format, D3DPOOL_DEFAULT,
                                   &out.color, nullptr),
             "CreateTexture(target-color)") ||
      !check(out.color->GetSurfaceLevel(0, &out.color_surface),
             "GetSurfaceLevel(target-color)") ||
      !check(device->CreateDepthStencilSurface(
                 spec.width, spec.height, D3DFMT_D24S8, D3DMULTISAMPLE_NONE, 0,
                 TRUE, &out.depth, nullptr),
             "CreateDepthStencilSurface(target-depth)") ||
      !check(device->CreateVertexBuffer(sizeof(kSceneVertices), 0, kFvf,
                                        D3DPOOL_DEFAULT, &out.vertices, nullptr),
             "CreateVertexBuffer(target-geometry)"))
    return false;

  void *mapped = nullptr;
  if (!check(out.vertices->Lock(0, sizeof(kSceneVertices), &mapped, 0),
             "Lock(target-geometry)"))
    return false;
  std::memcpy(mapped, kSceneVertices, sizeof(kSceneVertices));
  if (!check(out.vertices->Unlock(), "Unlock(target-geometry)"))
    return false;
  return true;
}

[[nodiscard]] bool render_frame(IDirect3DDevice9Ex *device,
                                SceneResources &resources,
                                GenerationSpec spec, std::uint32_t frame) {
  const auto world = world_matrix(frame);
  const auto identity = identity_matrix();
  D3DVIEWPORT9 viewport{0, 0, spec.width, spec.height, 0.0f, 1.0f};
  return check(device->SetRenderTarget(0, resources.color_surface.Get()),
               "SetRenderTarget(target-color)") &&
         check(device->SetDepthStencilSurface(resources.depth.Get()),
               "SetDepthStencilSurface(target-depth)") &&
         check(device->SetTransform(D3DTS_WORLD, &world),
               "SetTransform(target-world)") &&
         check(device->SetTransform(D3DTS_VIEW, &identity),
               "SetTransform(target-view)") &&
         check(device->SetTransform(D3DTS_PROJECTION, &identity),
               "SetTransform(target-projection)") &&
         check(device->SetViewport(&viewport), "SetViewport(target)") &&
         check(device->SetRenderState(D3DRS_LIGHTING, FALSE),
               "SetRenderState(target-lighting)") &&
         check(device->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE),
               "SetRenderState(target-cull)") &&
         check(device->SetRenderState(D3DRS_ZENABLE, D3DZB_TRUE),
               "SetRenderState(target-z)") &&
         check(device->SetRenderState(D3DRS_ZWRITEENABLE, TRUE),
               "SetRenderState(target-zwrite)") &&
         check(device->SetFVF(kFvf), "SetFVF(target)") &&
         check(device->SetStreamSource(0, resources.vertices.Get(), 0,
                                       sizeof(Vertex)),
               "SetStreamSource(target)") &&
         check(device->Clear(0, nullptr, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER,
                             D3DCOLOR_XRGB(20, 40, 120), 1.0f, 0),
               "Clear(target)") &&
         check(device->BeginScene(), "BeginScene(target)") &&
         check(device->DrawPrimitive(D3DPT_TRIANGLELIST, 0, 1),
               "DrawPrimitive(target-red)") &&
         check(device->DrawPrimitive(D3DPT_TRIANGLELIST, 3, 1),
               "DrawPrimitive(target-green)") &&
         check(device->EndScene(), "EndScene(target)");
}

} // namespace

int wmain(int argc, wchar_t **argv) {
  static_assert(sizeof(void *) == 4, "interception target must be built x86");
  D3DFORMAT color_format = D3DFMT_A2B10G10R10;
  bool use_interceptor = true;
  bool present = false;
  for (int i = 1; i < argc; ++i) {
    if (std::wcscmp(argv[i], L"--bgra8") == 0)
      color_format = D3DFMT_A8R8G8B8;
    else if (std::wcscmp(argv[i], L"--no-interceptor") == 0)
      use_interceptor = false;
    else if (std::wcscmp(argv[i], L"--present") == 0)
      present = true;
    else {
      std::cerr << "usage: ltr_d3d9ex_intercept_target.exe [--bgra8] "
                   "[--no-interceptor] [--present]\n";
      return 12;
    }
  }
  using InstallInterceptorFn = BOOL(WINAPI *)();
  using ShutdownInterceptorFn = void(WINAPI *)();
  HMODULE interceptor = nullptr;
  ShutdownInterceptorFn shutdown = nullptr;
  if (use_interceptor) {
    interceptor = LoadLibraryW(L"ltr_d3d9ex_interceptor.dll");
    if (!interceptor) {
      std::cerr << "LoadLibraryW(interceptor) failed error=" << GetLastError()
                << "\n";
      return 1;
    }
    auto install = reinterpret_cast<InstallInterceptorFn>(
        GetProcAddress(interceptor, "LtrInstallD3D9ExInterceptor"));
    if (!install)
      install = reinterpret_cast<InstallInterceptorFn>(
          GetProcAddress(interceptor, "_LtrInstallD3D9ExInterceptor@0"));
    if (!install || !install()) {
      std::cerr << "Install D3D9Ex interceptor failed error=" << GetLastError()
                << "\n";
      return 1;
    }
    shutdown = reinterpret_cast<ShutdownInterceptorFn>(
        GetProcAddress(interceptor, "LtrShutdownD3D9ExInterceptor"));
    if (!shutdown)
      shutdown = reinterpret_cast<ShutdownInterceptorFn>(
          GetProcAddress(interceptor, "_LtrShutdownD3D9ExInterceptor@0"));
    if (!shutdown) {
      std::cerr << "Locate D3D9Ex interceptor shutdown failed error="
                << GetLastError() << "\n";
      return 1;
    }
  }
  const HWND hwnd = create_hidden_window();
  if (!hwnd)
    return 2;
  if (present) {
    SetWindowPos(hwnd, nullptr, 0, 0, static_cast<int>(kGenerations[0].width),
                 static_cast<int>(kGenerations[0].height),
                 SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    ShowWindow(hwnd, SW_SHOWNOACTIVATE);
    UpdateWindow(hwnd);
  }

  ComPtr<IDirect3D9Ex> d3d9;
  if (!check(Direct3DCreate9Ex(D3D_SDK_VERSION, &d3d9),
             "Direct3DCreate9Ex(target)")) {
    DestroyWindow(hwnd);
    return 3;
  }

  D3DPRESENT_PARAMETERS pp{};
  pp.Windowed = TRUE;
  pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
  pp.hDeviceWindow = hwnd;
  pp.BackBufferFormat = D3DFMT_UNKNOWN;
  pp.BackBufferWidth = present ? kGenerations[0].width : 64U;
  pp.BackBufferHeight = present ? kGenerations[0].height : 64U;
  pp.PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE;

  ComPtr<IDirect3DDevice9Ex> device;
  HRESULT create_hr = d3d9->CreateDeviceEx(
      D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, hwnd,
      D3DCREATE_HARDWARE_VERTEXPROCESSING | D3DCREATE_MULTITHREADED, &pp,
      nullptr, &device);
  if (FAILED(create_hr))
    create_hr = d3d9->CreateDeviceEx(
        D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, hwnd,
        D3DCREATE_SOFTWARE_VERTEXPROCESSING | D3DCREATE_MULTITHREADED, &pp,
        nullptr, &device);
  if (!check(create_hr, "CreateDeviceEx(target)")) {
    DestroyWindow(hwnd);
    return 4;
  }

  ComPtr<IDirect3DSurface9> default_rt;
  if (!check(device->GetRenderTarget(0, &default_rt),
             "GetRenderTarget(target-default)")) {
    DestroyWindow(hwnd);
    return 5;
  }

  std::uint32_t frame = 0;
  std::uint32_t resets = 0;
  for (std::uint32_t generation = 0; generation < std::size(kGenerations);
       ++generation) {
    SceneResources resources;
    if (!create_scene_resources(device.Get(), kGenerations[generation],
                                color_format, resources)) {
      DestroyWindow(hwnd);
      return 6;
    }
    for (std::uint32_t local = 0; local < kFramesPerGeneration;
         ++local, ++frame) {
      if (!render_frame(device.Get(), resources, kGenerations[generation],
                        frame)) {
        DestroyWindow(hwnd);
        return 7;
      }
      if (present &&
          (!check(device->StretchRect(resources.color_surface.Get(), nullptr,
                                      default_rt.Get(), nullptr,
                                      D3DTEXF_NONE),
                  "StretchRect(target-present)") ||
           !check(device->PresentEx(nullptr, nullptr, nullptr, nullptr, 0),
                  "PresentEx(target)"))) {
        DestroyWindow(hwnd);
        return 13;
      }
      if (present) {
        MSG msg{};
        while (PeekMessageW(&msg, hwnd, 0, 0, PM_REMOVE)) {
          TranslateMessage(&msg);
          DispatchMessageW(&msg);
        }
        Sleep(16);
      }
    }

    if (!check(device->SetRenderTarget(0, default_rt.Get()),
               "SetRenderTarget(target-default-before-reset)") ||
        !check(device->SetDepthStencilSurface(nullptr),
               "SetDepthStencilSurface(target-null-before-reset)")) {
      DestroyWindow(hwnd);
      return 8;
    }
    resources = {};
    if (generation + 1U < std::size(kGenerations)) {
      default_rt.Reset();
      if (present) {
        pp.BackBufferWidth = kGenerations[generation + 1U].width;
        pp.BackBufferHeight = kGenerations[generation + 1U].height;
      } else {
        pp.BackBufferWidth += 16U;
        pp.BackBufferHeight += 16U;
      }
      if (!check(device->ResetEx(&pp, nullptr), "ResetEx(target)")) {
        DestroyWindow(hwnd);
        return 9;
      }
      ++resets;
      if (!check(device->GetRenderTarget(0, &default_rt),
                 "GetRenderTarget(target-default-after-reset)")) {
        DestroyWindow(hwnd);
        return 10;
      }
    }
  }

  std::cout << "target_frames=" << frame << " target_resets=" << resets
            << " color_format=" << static_cast<unsigned>(color_format)
            << " interceptor=" << use_interceptor << " present=" << present
            << " d3d11_loaded=" << (GetModuleHandleW(L"d3d11.dll") != nullptr)
            << " d3d12_loaded=" << (GetModuleHandleW(L"d3d12.dll") != nullptr)
            << " RESULT PASS\n";
  default_rt.Reset();
  device.Reset();
  d3d9.Reset();
  if (shutdown)
    shutdown();
  DestroyWindow(hwnd);
  return frame == 12 && resets == 1 ? 0 : 11;
}
