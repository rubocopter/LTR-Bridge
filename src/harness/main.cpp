#include "temporal_frame.h"

#include <Windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <DirectXMath.h>
#include <wrl/client.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cwchar>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

using Microsoft::WRL::ComPtr;
using namespace DirectX;

namespace {

constexpr wchar_t kWindowClass[] = L"LTRBridgeTemporalHarness";
constexpr wchar_t kWindowTitle[] = L"LTR Bridge - D3D11 x64 Temporal Harness";

enum class Scenario : std::uint32_t { static_scene = 0, camera_translate, camera_rotate, rigid_object, disocclusion, count };
enum class DebugView : std::uint32_t { scene = 0, depth, motion, reconstructed_motion, count };

struct Vertex { XMFLOAT3 position; XMFLOAT3 color; };

struct PerDrawConstants {
    XMFLOAT4X4 current_world;
    XMFLOAT4X4 previous_world;
    XMFLOAT4X4 current_vp_jittered;
    XMFLOAT4X4 current_vp_unjittered;
    XMFLOAT4X4 previous_vp_unjittered;
    XMFLOAT2 render_size;
    std::uint32_t surface_id = 0;
    std::uint32_t padding = 0;
};

struct DebugConstants {
    std::uint32_t mode = 0;
    float motion_scale = 16.0f;
    float padding[2]{};
};

struct ReconstructConstants {
    XMFLOAT4X4 inverse_current_vp_jittered;
    XMFLOAT4X4 current_vp_unjittered;
    XMFLOAT4X4 previous_vp_unjittered;
    XMFLOAT2 render_size;
    XMFLOAT2 padding{};
};

static_assert(sizeof(PerDrawConstants) % 16 == 0);
static_assert(sizeof(DebugConstants) % 16 == 0);
static_assert(sizeof(ReconstructConstants) % 16 == 0);

HWND g_window = nullptr;
std::uint32_t g_width = 1280;
std::uint32_t g_height = 720;
std::uint32_t g_pending_width = 0;
std::uint32_t g_pending_height = 0;
Scenario g_scenario = Scenario::static_scene;
DebugView g_debug_view = DebugView::scene;
std::uint64_t g_frame_index = 0;
std::uint32_t g_history_generation = 0;
ltr::harness::HistoryResetReason g_pending_reset = ltr::harness::HistoryResetReason::startup;
bool g_previous_valid = false;
bool g_self_test = false;
XMMATRIX g_previous_world = XMMatrixIdentity();
XMMATRIX g_previous_vp = XMMatrixIdentity();
float g_previous_jitter_x = 0.0f;
float g_previous_jitter_y = 0.0f;
float g_last_previous_jitter_x = 0.0f;
float g_last_previous_jitter_y = 0.0f;
std::vector<std::uint32_t> g_previous_surface_ids;
ltr::harness::TemporalFrameDescription g_last_frame{};
float g_cpu_probe_motion = 0.0f;

ComPtr<ID3D11Device> g_device;
ComPtr<ID3D11DeviceContext> g_context;
ComPtr<IDXGISwapChain> g_swap_chain;
ComPtr<ID3D11RenderTargetView> g_backbuffer_rtv;
ComPtr<ID3D11Texture2D> g_scene_color;
ComPtr<ID3D11RenderTargetView> g_scene_color_rtv;
ComPtr<ID3D11ShaderResourceView> g_scene_color_srv;
ComPtr<ID3D11Texture2D> g_motion;
ComPtr<ID3D11RenderTargetView> g_motion_rtv;
ComPtr<ID3D11ShaderResourceView> g_motion_srv;
ComPtr<ID3D11Texture2D> g_surface_id;
ComPtr<ID3D11RenderTargetView> g_surface_id_rtv;
ComPtr<ID3D11Texture2D> g_reconstructed_motion;
ComPtr<ID3D11RenderTargetView> g_reconstructed_motion_rtv;
ComPtr<ID3D11ShaderResourceView> g_reconstructed_motion_srv;
ComPtr<ID3D11Texture2D> g_depth;
ComPtr<ID3D11DepthStencilView> g_depth_dsv;
ComPtr<ID3D11ShaderResourceView> g_depth_srv;
ComPtr<ID3D11VertexShader> g_scene_vs;
ComPtr<ID3D11PixelShader> g_scene_ps;
ComPtr<ID3D11VertexShader> g_debug_vs;
ComPtr<ID3D11PixelShader> g_debug_ps;
ComPtr<ID3D11PixelShader> g_reconstruct_ps;
ComPtr<ID3D11InputLayout> g_input_layout;
ComPtr<ID3D11Buffer> g_vertex_buffer;
ComPtr<ID3D11Buffer> g_per_draw_buffer;
ComPtr<ID3D11Buffer> g_debug_buffer;
ComPtr<ID3D11Buffer> g_reconstruct_buffer;
ComPtr<ID3D11SamplerState> g_sampler;

struct ReadbackStats {
    std::uint64_t active_pixels = 0;
    std::uint64_t moving_pixels = 0;
    float min_depth = 1.0f;
    float max_motion = 0.0f;
    double mean_motion = 0.0;
};

struct HistoryValidityStats {
    std::uint64_t active_pixels = 0;
    std::uint64_t valid_pixels = 0;
    std::uint64_t invalid_pixels = 0;
    std::uint64_t surface_mismatch_pixels = 0;
    std::uint64_t out_of_bounds_pixels = 0;
    std::uint64_t disoccluded_background_pixels = 0;
};

struct ReconstructionStats {
    std::uint64_t active_pixels = 0;
    std::uint64_t ground_truth_moving_pixels = 0;
    std::uint64_t reconstructed_moving_pixels = 0;
    std::uint64_t close_pixels = 0;
    std::uint64_t camera_only_miss_pixels = 0;
    float max_error = 0.0f;
    float max_reconstructed_motion = 0.0f;
    double mean_error = 0.0;
    double mean_reconstructed_motion = 0.0;
};

std::uint8_t ToByte(float value) {
    return static_cast<std::uint8_t>(std::lround((std::clamp)(value, 0.0f, 1.0f) * 255.0f));
}

std::uint32_t PackBgra(float red, float green, float blue) {
    return 0xFF000000u | (static_cast<std::uint32_t>(ToByte(red)) << 16u) |
           (static_cast<std::uint32_t>(ToByte(green)) << 8u) | static_cast<std::uint32_t>(ToByte(blue));
}

void WriteBitmap32(const std::string& path, const std::vector<std::uint32_t>& pixels) {
    BITMAPFILEHEADER fileHeader{};
    BITMAPINFOHEADER infoHeader{};
    const std::uint32_t dataSize = g_width * g_height * sizeof(std::uint32_t);
    fileHeader.bfType = 0x4D42;
    fileHeader.bfOffBits = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER);
    fileHeader.bfSize = fileHeader.bfOffBits + dataSize;
    infoHeader.biSize = sizeof(BITMAPINFOHEADER);
    infoHeader.biWidth = static_cast<LONG>(g_width);
    infoHeader.biHeight = -static_cast<LONG>(g_height);
    infoHeader.biPlanes = 1;
    infoHeader.biBitCount = 32;
    infoHeader.biCompression = BI_RGB;
    infoHeader.biSizeImage = dataSize;

    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output.write(reinterpret_cast<const char*>(&fileHeader), sizeof(fileHeader));
    output.write(reinterpret_cast<const char*>(&infoHeader), sizeof(infoHeader));
    output.write(reinterpret_cast<const char*>(pixels.data()), static_cast<std::streamsize>(dataSize));
}

[[noreturn]] void ThrowHr(const char* operation, HRESULT hr) {
    char message[256]{};
    sprintf_s(message, "%s failed (HRESULT 0x%08X)", operation, static_cast<unsigned>(hr));
    throw std::runtime_error(message);
}

void CheckHr(HRESULT hr, const char* operation) {
    if (FAILED(hr)) ThrowHr(operation, hr);
}

ComPtr<ID3DBlob> Compile(std::string_view source, const char* entry, const char* target) {
    ComPtr<ID3DBlob> bytecode;
    ComPtr<ID3DBlob> errors;
    const HRESULT hr = D3DCompile(source.data(), source.size(), nullptr, nullptr, nullptr, entry, target,
                                  D3DCOMPILE_ENABLE_STRICTNESS, 0, &bytecode, &errors);
    if (FAILED(hr)) {
        if (errors) OutputDebugStringA(static_cast<const char*>(errors->GetBufferPointer()));
        ThrowHr(entry, hr);
    }
    return bytecode;
}

float Halton(std::uint64_t index, std::uint32_t base) {
    float result = 0.0f;
    float fraction = 1.0f;
    while (index > 0) {
        fraction /= static_cast<float>(base);
        result += fraction * static_cast<float>(index % base);
        index /= base;
    }
    return result;
}

XMMATRIX JitterProjection(const XMMATRIX& projection, float xPixels, float yPixels) {
    XMFLOAT4X4 value{};
    XMStoreFloat4x4(&value, projection);
    value._31 += (2.0f * xPixels) / static_cast<float>(g_width);
    value._32 -= (2.0f * yPixels) / static_cast<float>(g_height);
    return XMLoadFloat4x4(&value);
}

constexpr std::string_view kSceneShader = R"hlsl(
cbuffer PerDraw : register(b0) {
    row_major float4x4 currentWorld;
    row_major float4x4 previousWorld;
    row_major float4x4 currentVpJittered;
    row_major float4x4 currentVpUnjittered;
    row_major float4x4 previousVpUnjittered;
    float2 renderSize;
    uint surfaceId;
    uint padding;
};
struct VSIn { float3 position : POSITION; float3 color : COLOR0; };
struct VSOut {
    float4 position : SV_Position;
    float4 currentNoJitter : TEXCOORD0;
    float4 previousNoJitter : TEXCOORD1;
    float3 color : COLOR0;
};
VSOut SceneVS(VSIn input) {
    VSOut output;
    float4 local = float4(input.position, 1.0);
    float4 currentWorldPos = mul(local, currentWorld);
    float4 previousWorldPos = mul(local, previousWorld);
    output.position = mul(currentWorldPos, currentVpJittered);
    output.currentNoJitter = mul(currentWorldPos, currentVpUnjittered);
    output.previousNoJitter = mul(previousWorldPos, previousVpUnjittered);
    output.color = input.color;
    return output;
}
struct PSOut { float4 color : SV_Target0; float2 motion : SV_Target1; uint surface : SV_Target2; };
PSOut ScenePS(VSOut input) {
    PSOut output;
    output.color = float4(input.color, 1.0);
    float2 currentNdc = input.currentNoJitter.xy / input.currentNoJitter.w;
    float2 previousNdc = input.previousNoJitter.xy / input.previousNoJitter.w;
    float2 currentPixels = float2((currentNdc.x * 0.5 + 0.5) * renderSize.x,
                                  (-currentNdc.y * 0.5 + 0.5) * renderSize.y);
    float2 previousPixels = float2((previousNdc.x * 0.5 + 0.5) * renderSize.x,
                                   (-previousNdc.y * 0.5 + 0.5) * renderSize.y);
    output.motion = previousPixels - currentPixels;
    output.surface = surfaceId;
    return output;
}
)hlsl";

constexpr std::string_view kDebugShader = R"hlsl(
Texture2D<float4> sceneTexture : register(t0);
Texture2D<float> depthTexture : register(t1);
Texture2D<float2> motionTexture : register(t2);
Texture2D<float2> reconstructedMotionTexture : register(t3);
SamplerState linearClamp : register(s0);
cbuffer DebugConstants : register(b0) {
    uint mode;
    float motionScale;
    float2 padding;
};
struct VSOut { float4 position : SV_Position; float2 uv : TEXCOORD0; };
VSOut DebugVS(uint vertexId : SV_VertexID) {
    VSOut output;
    float2 uv = float2((vertexId << 1) & 2, vertexId & 2);
    output.uv = uv;
    output.position = float4(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0, 0.0, 1.0);
    return output;
}
float4 DebugPS(VSOut input) : SV_Target0 {
    if (mode == 0) return sceneTexture.Sample(linearClamp, input.uv);
    if (mode == 1) {
        float depth = depthTexture.Sample(linearClamp, input.uv);
        float visual = saturate((1.0 - depth) * 12.0);
        return float4(visual.xxx, 1.0);
    }
    float2 motion = mode == 2 ? motionTexture.Sample(linearClamp, input.uv)
                              : reconstructedMotionTexture.Sample(linearClamp, input.uv);
    float2 signedMotion = clamp(motion / motionScale, -1.0, 1.0);
    float magnitude = saturate(length(motion) / motionScale);
    return float4(0.5 + 0.5 * signedMotion.x, 0.5 + 0.5 * signedMotion.y, magnitude, 1.0);
}
)hlsl";

constexpr std::string_view kReconstructShader = R"hlsl(
Texture2D<float> depthTexture : register(t0);
cbuffer ReconstructConstants : register(b0) {
    row_major float4x4 inverseCurrentVpJittered;
    row_major float4x4 currentVpUnjittered;
    row_major float4x4 previousVpUnjittered;
    float2 renderSize;
    float2 padding;
};
struct PSIn { float4 position : SV_Position; float2 uv : TEXCOORD0; };
float2 ReconstructPS(PSIn input) : SV_Target0 {
    int2 pixel = int2(input.position.xy);
    float depth = depthTexture.Load(int3(pixel, 0));
    if (depth >= 0.99999) return float2(0.0, 0.0);

    float2 currentNdc = float2((input.position.x / renderSize.x) * 2.0 - 1.0,
                               1.0 - (input.position.y / renderSize.y) * 2.0);
    float4 currentClipJittered = float4(currentNdc, depth, 1.0);
    float4 world = mul(currentClipJittered, inverseCurrentVpJittered);
    world /= world.w;

    float4 currentClip = mul(world, currentVpUnjittered);
    float4 previousClip = mul(world, previousVpUnjittered);
    float2 currentUnjitteredNdc = currentClip.xy / currentClip.w;
    float2 previousNdc = previousClip.xy / previousClip.w;
    float2 currentPixels = float2((currentUnjitteredNdc.x * 0.5 + 0.5) * renderSize.x,
                                  (-currentUnjitteredNdc.y * 0.5 + 0.5) * renderSize.y);
    float2 previousPixels = float2((previousNdc.x * 0.5 + 0.5) * renderSize.x,
                                   (-previousNdc.y * 0.5 + 0.5) * renderSize.y);
    return previousPixels - currentPixels;
}
)hlsl";

void CreateFrameResources(std::uint32_t width, std::uint32_t height) {
    ID3D11ShaderResourceView* nullSrvs[4]{};
    g_context->PSSetShaderResources(0, 4, nullSrvs);
    g_context->OMSetRenderTargets(0, nullptr, nullptr);
    g_backbuffer_rtv.Reset();
    g_scene_color.Reset(); g_scene_color_rtv.Reset(); g_scene_color_srv.Reset();
    g_motion.Reset(); g_motion_rtv.Reset(); g_motion_srv.Reset();
    g_surface_id.Reset(); g_surface_id_rtv.Reset();
    g_reconstructed_motion.Reset(); g_reconstructed_motion_rtv.Reset(); g_reconstructed_motion_srv.Reset();
    g_depth.Reset(); g_depth_dsv.Reset(); g_depth_srv.Reset();

    CheckHr(g_swap_chain->ResizeBuffers(0, width, height, DXGI_FORMAT_UNKNOWN, 0), "ResizeBuffers");
    ComPtr<ID3D11Texture2D> backbuffer;
    CheckHr(g_swap_chain->GetBuffer(0, IID_PPV_ARGS(&backbuffer)), "GetBuffer");
    CheckHr(g_device->CreateRenderTargetView(backbuffer.Get(), nullptr, &g_backbuffer_rtv), "CreateRTV(backbuffer)");

    D3D11_TEXTURE2D_DESC color{};
    color.Width = width; color.Height = height; color.MipLevels = 1; color.ArraySize = 1;
    color.Format = DXGI_FORMAT_R8G8B8A8_UNORM; color.SampleDesc.Count = 1;
    color.Usage = D3D11_USAGE_DEFAULT; color.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    CheckHr(g_device->CreateTexture2D(&color, nullptr, &g_scene_color), "CreateTexture2D(scene)");
    CheckHr(g_device->CreateRenderTargetView(g_scene_color.Get(), nullptr, &g_scene_color_rtv), "CreateRTV(scene)");
    CheckHr(g_device->CreateShaderResourceView(g_scene_color.Get(), nullptr, &g_scene_color_srv), "CreateSRV(scene)");

    color.Format = DXGI_FORMAT_R32G32_FLOAT;
    CheckHr(g_device->CreateTexture2D(&color, nullptr, &g_motion), "CreateTexture2D(motion)");
    CheckHr(g_device->CreateRenderTargetView(g_motion.Get(), nullptr, &g_motion_rtv), "CreateRTV(motion)");
    CheckHr(g_device->CreateShaderResourceView(g_motion.Get(), nullptr, &g_motion_srv), "CreateSRV(motion)");

    D3D11_TEXTURE2D_DESC surfaceId = color;
    surfaceId.Format = DXGI_FORMAT_R32_UINT;
    surfaceId.BindFlags = D3D11_BIND_RENDER_TARGET;
    CheckHr(g_device->CreateTexture2D(&surfaceId, nullptr, &g_surface_id), "CreateTexture2D(surface id)");
    CheckHr(g_device->CreateRenderTargetView(g_surface_id.Get(), nullptr, &g_surface_id_rtv), "CreateRTV(surface id)");

    CheckHr(g_device->CreateTexture2D(&color, nullptr, &g_reconstructed_motion), "CreateTexture2D(reconstructed motion)");
    CheckHr(g_device->CreateRenderTargetView(g_reconstructed_motion.Get(), nullptr, &g_reconstructed_motion_rtv), "CreateRTV(reconstructed motion)");
    CheckHr(g_device->CreateShaderResourceView(g_reconstructed_motion.Get(), nullptr, &g_reconstructed_motion_srv), "CreateSRV(reconstructed motion)");

    D3D11_TEXTURE2D_DESC depth{};
    depth.Width = width; depth.Height = height; depth.MipLevels = 1; depth.ArraySize = 1;
    depth.Format = DXGI_FORMAT_R32_TYPELESS; depth.SampleDesc.Count = 1;
    depth.Usage = D3D11_USAGE_DEFAULT; depth.BindFlags = D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE;
    CheckHr(g_device->CreateTexture2D(&depth, nullptr, &g_depth), "CreateTexture2D(depth)");
    D3D11_DEPTH_STENCIL_VIEW_DESC dsv{}; dsv.Format = DXGI_FORMAT_D32_FLOAT; dsv.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
    CheckHr(g_device->CreateDepthStencilView(g_depth.Get(), &dsv, &g_depth_dsv), "CreateDSV(depth)");
    D3D11_SHADER_RESOURCE_VIEW_DESC srv{}; srv.Format = DXGI_FORMAT_R32_FLOAT; srv.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D; srv.Texture2D.MipLevels = 1;
    CheckHr(g_device->CreateShaderResourceView(g_depth.Get(), &srv, &g_depth_srv), "CreateSRV(depth)");

    g_width = width; g_height = height; g_previous_valid = false; g_previous_surface_ids.clear();
}

void CreateDeviceAndPipeline() {
    DXGI_SWAP_CHAIN_DESC swap{};
    swap.BufferDesc.Width = g_width; swap.BufferDesc.Height = g_height; swap.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    swap.SampleDesc.Count = 1; swap.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT; swap.BufferCount = 2;
    swap.OutputWindow = g_window; swap.Windowed = TRUE; swap.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
    const D3D_FEATURE_LEVEL levels[] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_10_0 };
    D3D_FEATURE_LEVEL obtained{};
    HRESULT hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT,
                                                levels, 3, D3D11_SDK_VERSION, &swap, &g_swap_chain, &g_device, &obtained, &g_context);
    if (FAILED(hr)) {
        hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT,
                                           levels, 3, D3D11_SDK_VERSION, &swap, &g_swap_chain, &g_device, &obtained, &g_context);
    }
    CheckHr(hr, "D3D11CreateDeviceAndSwapChain");

    const auto sceneVs = Compile(kSceneShader, "SceneVS", "vs_4_0");
    const auto scenePs = Compile(kSceneShader, "ScenePS", "ps_4_0");
    const auto debugVs = Compile(kDebugShader, "DebugVS", "vs_4_0");
    const auto debugPs = Compile(kDebugShader, "DebugPS", "ps_4_0");
    const auto reconstructPs = Compile(kReconstructShader, "ReconstructPS", "ps_4_0");
    CheckHr(g_device->CreateVertexShader(sceneVs->GetBufferPointer(), sceneVs->GetBufferSize(), nullptr, &g_scene_vs), "CreateVertexShader");
    CheckHr(g_device->CreatePixelShader(scenePs->GetBufferPointer(), scenePs->GetBufferSize(), nullptr, &g_scene_ps), "CreatePixelShader");
    CheckHr(g_device->CreateVertexShader(debugVs->GetBufferPointer(), debugVs->GetBufferSize(), nullptr, &g_debug_vs), "CreateDebugVS");
    CheckHr(g_device->CreatePixelShader(debugPs->GetBufferPointer(), debugPs->GetBufferSize(), nullptr, &g_debug_ps), "CreateDebugPS");
    CheckHr(g_device->CreatePixelShader(reconstructPs->GetBufferPointer(), reconstructPs->GetBufferSize(), nullptr, &g_reconstruct_ps), "CreateReconstructPS");

    const D3D11_INPUT_ELEMENT_DESC elements[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, static_cast<UINT>(offsetof(Vertex, position)), D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "COLOR", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, static_cast<UINT>(offsetof(Vertex, color)), D3D11_INPUT_PER_VERTEX_DATA, 0 },
    };
    CheckHr(g_device->CreateInputLayout(elements, 2, sceneVs->GetBufferPointer(), sceneVs->GetBufferSize(), &g_input_layout), "CreateInputLayout");

    constexpr std::array<Vertex, 6> vertices{{
        {{-1.1f, -0.8f, 0.0f}, {0.9f, 0.2f, 0.2f}}, {{0.0f, 1.0f, 0.0f}, {0.2f, 0.9f, 0.3f}}, {{1.1f, -0.8f, 0.0f}, {0.2f, 0.3f, 0.95f}},
        {{-4.0f, -1.4f, -2.0f}, {0.25f, 0.27f, 0.31f}}, {{0.0f, -1.4f, 5.0f}, {0.20f, 0.22f, 0.26f}}, {{4.0f, -1.4f, -2.0f}, {0.32f, 0.34f, 0.38f}},
    }};
    D3D11_BUFFER_DESC vb{}; vb.ByteWidth = sizeof(vertices); vb.Usage = D3D11_USAGE_IMMUTABLE; vb.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    D3D11_SUBRESOURCE_DATA init{}; init.pSysMem = vertices.data();
    CheckHr(g_device->CreateBuffer(&vb, &init, &g_vertex_buffer), "CreateBuffer(vertices)");

    D3D11_BUFFER_DESC cb{}; cb.ByteWidth = sizeof(PerDrawConstants); cb.Usage = D3D11_USAGE_DYNAMIC; cb.BindFlags = D3D11_BIND_CONSTANT_BUFFER; cb.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    CheckHr(g_device->CreateBuffer(&cb, nullptr, &g_per_draw_buffer), "CreateBuffer(per-draw)");
    cb.ByteWidth = sizeof(DebugConstants);
    CheckHr(g_device->CreateBuffer(&cb, nullptr, &g_debug_buffer), "CreateBuffer(debug)");
    cb.ByteWidth = sizeof(ReconstructConstants);
    CheckHr(g_device->CreateBuffer(&cb, nullptr, &g_reconstruct_buffer), "CreateBuffer(reconstruct)");

    D3D11_SAMPLER_DESC sampler{}; sampler.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sampler.AddressU = sampler.AddressV = sampler.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP; sampler.MaxLOD = D3D11_FLOAT32_MAX;
    CheckHr(g_device->CreateSamplerState(&sampler, &g_sampler), "CreateSamplerState");
    CreateFrameResources(g_width, g_height);
}

void UploadPerDraw(const XMMATRIX& world, const XMMATRIX& previousWorld, const XMMATRIX& currentJittered,
                   const XMMATRIX& currentUnjittered, const XMMATRIX& previousVp, std::uint32_t surfaceId) {
    PerDrawConstants constants{};
    XMStoreFloat4x4(&constants.current_world, world); XMStoreFloat4x4(&constants.previous_world, previousWorld);
    XMStoreFloat4x4(&constants.current_vp_jittered, currentJittered); XMStoreFloat4x4(&constants.current_vp_unjittered, currentUnjittered);
    XMStoreFloat4x4(&constants.previous_vp_unjittered, previousVp);
    constants.render_size = { static_cast<float>(g_width), static_cast<float>(g_height) };
    constants.surface_id = surfaceId;
    D3D11_MAPPED_SUBRESOURCE mapped{};
    CheckHr(g_context->Map(g_per_draw_buffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped), "Map(per-draw)");
    std::memcpy(mapped.pData, &constants, sizeof(constants)); g_context->Unmap(g_per_draw_buffer.Get(), 0);
    ID3D11Buffer* buffer = g_per_draw_buffer.Get(); g_context->VSSetConstantBuffers(0, 1, &buffer);
    g_context->PSSetConstantBuffers(0, 1, &buffer);
}

ComPtr<ID3D11Texture2D> CreateReadbackTexture(ID3D11Texture2D* source) {
    D3D11_TEXTURE2D_DESC desc{};
    source->GetDesc(&desc);
    desc.Usage = D3D11_USAGE_STAGING;
    desc.BindFlags = 0;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    desc.MiscFlags = 0;
    ComPtr<ID3D11Texture2D> staging;
    CheckHr(g_device->CreateTexture2D(&desc, nullptr, &staging), "CreateTexture2D(readback)");
    return staging;
}

ReadbackStats ReadbackGroundTruth(const char* diagnosticLabel = nullptr) {
    const auto depthReadback = CreateReadbackTexture(g_depth.Get());
    const auto motionReadback = CreateReadbackTexture(g_motion.Get());
    g_context->CopyResource(depthReadback.Get(), g_depth.Get());
    g_context->CopyResource(motionReadback.Get(), g_motion.Get());

    D3D11_MAPPED_SUBRESOURCE depthMapped{};
    D3D11_MAPPED_SUBRESOURCE motionMapped{};
    CheckHr(g_context->Map(depthReadback.Get(), 0, D3D11_MAP_READ, 0, &depthMapped), "Map(depth readback)");
    const HRESULT motionMapHr = g_context->Map(motionReadback.Get(), 0, D3D11_MAP_READ, 0, &motionMapped);
    if (FAILED(motionMapHr)) {
        g_context->Unmap(depthReadback.Get(), 0);
        ThrowHr("Map(motion readback)", motionMapHr);
    }

    ReadbackStats stats{};
    double motionSum = 0.0;
    std::vector<std::uint32_t> depthPixels;
    std::vector<std::uint32_t> motionPixels;
    if (diagnosticLabel) {
        depthPixels.resize(static_cast<std::size_t>(g_width) * g_height);
        motionPixels.resize(static_cast<std::size_t>(g_width) * g_height);
    }
    for (std::uint32_t y = 0; y < g_height; ++y) {
        const auto* depthRow = reinterpret_cast<const float*>(static_cast<const std::byte*>(depthMapped.pData) + y * depthMapped.RowPitch);
        const auto* motionRow = reinterpret_cast<const XMFLOAT2*>(static_cast<const std::byte*>(motionMapped.pData) + y * motionMapped.RowPitch);
        for (std::uint32_t x = 0; x < g_width; ++x) {
            const float depth = depthRow[x];
            if (!std::isfinite(depth) || depth >= 0.99999f) continue;
            ++stats.active_pixels;
            stats.min_depth = (std::min)(stats.min_depth, depth);
            const float magnitude = std::hypot(motionRow[x].x, motionRow[x].y);
            if (!std::isfinite(magnitude)) {
                g_context->Unmap(motionReadback.Get(), 0);
                g_context->Unmap(depthReadback.Get(), 0);
                throw std::runtime_error("non-finite motion vector found during readback");
            }
            stats.max_motion = (std::max)(stats.max_motion, magnitude);
            motionSum += magnitude;
            if (magnitude > 0.05f) ++stats.moving_pixels;
            if (diagnosticLabel) {
                const std::size_t index = static_cast<std::size_t>(y) * g_width + x;
                const float depthVisual = std::pow((std::clamp)(1.0f - depth, 0.0f, 1.0f), 0.2f);
                depthPixels[index] = PackBgra(depthVisual, depthVisual, depthVisual);
                const float motionX = (std::clamp)(motionRow[x].x / 128.0f, -1.0f, 1.0f);
                const float motionY = (std::clamp)(motionRow[x].y / 128.0f, -1.0f, 1.0f);
                motionPixels[index] = PackBgra(0.5f + 0.5f * motionX, 0.5f + 0.5f * motionY,
                                               (std::min)(magnitude / 128.0f, 1.0f));
            }
        }
    }
    if (stats.active_pixels != 0) stats.mean_motion = motionSum / static_cast<double>(stats.active_pixels);

    if (diagnosticLabel) {
        const std::string prefix = std::string("ltr_diag_") + diagnosticLabel;
        WriteBitmap32(prefix + "_depth.bmp", depthPixels);
        WriteBitmap32(prefix + "_motion.bmp", motionPixels);
    }

    g_context->Unmap(motionReadback.Get(), 0);
    g_context->Unmap(depthReadback.Get(), 0);
    return stats;
}

HistoryValidityStats ReadbackHistoryValidity(bool historyAvailable, const char* diagnosticLabel = nullptr) {
    const auto depthReadback = CreateReadbackTexture(g_depth.Get());
    const auto motionReadback = CreateReadbackTexture(g_motion.Get());
    const auto surfaceReadback = CreateReadbackTexture(g_surface_id.Get());
    g_context->CopyResource(depthReadback.Get(), g_depth.Get());
    g_context->CopyResource(motionReadback.Get(), g_motion.Get());
    g_context->CopyResource(surfaceReadback.Get(), g_surface_id.Get());

    D3D11_MAPPED_SUBRESOURCE depthMapped{};
    D3D11_MAPPED_SUBRESOURCE motionMapped{};
    D3D11_MAPPED_SUBRESOURCE surfaceMapped{};
    CheckHr(g_context->Map(depthReadback.Get(), 0, D3D11_MAP_READ, 0, &depthMapped), "Map(depth validity)");
    HRESULT hr = g_context->Map(motionReadback.Get(), 0, D3D11_MAP_READ, 0, &motionMapped);
    if (FAILED(hr)) {
        g_context->Unmap(depthReadback.Get(), 0);
        ThrowHr("Map(motion validity)", hr);
    }
    hr = g_context->Map(surfaceReadback.Get(), 0, D3D11_MAP_READ, 0, &surfaceMapped);
    if (FAILED(hr)) {
        g_context->Unmap(motionReadback.Get(), 0);
        g_context->Unmap(depthReadback.Get(), 0);
        ThrowHr("Map(surface validity)", hr);
    }

    const std::size_t pixelCount = static_cast<std::size_t>(g_width) * g_height;
    std::vector<std::uint32_t> currentSurfaceIds(pixelCount, 0u);
    std::vector<std::uint32_t> diagnosticPixels;
    if (diagnosticLabel) diagnosticPixels.resize(pixelCount);
    HistoryValidityStats stats{};
    const bool previousSurfaceAvailable = historyAvailable && g_previous_surface_ids.size() == pixelCount;
    const float jitterDeltaX = g_last_previous_jitter_x - g_last_frame.jitter_x_pixels;
    const float jitterDeltaY = g_last_previous_jitter_y - g_last_frame.jitter_y_pixels;

    for (std::uint32_t y = 0; y < g_height; ++y) {
        const auto* depthRow = reinterpret_cast<const float*>(static_cast<const std::byte*>(depthMapped.pData) + y * depthMapped.RowPitch);
        const auto* motionRow = reinterpret_cast<const XMFLOAT2*>(static_cast<const std::byte*>(motionMapped.pData) + y * motionMapped.RowPitch);
        const auto* surfaceRow = reinterpret_cast<const std::uint32_t*>(static_cast<const std::byte*>(surfaceMapped.pData) + y * surfaceMapped.RowPitch);
        for (std::uint32_t x = 0; x < g_width; ++x) {
            const std::size_t index = static_cast<std::size_t>(y) * g_width + x;
            const float depth = depthRow[x];
            if (!std::isfinite(depth) || depth >= 0.99999f) continue;
            const std::uint32_t currentSurface = surfaceRow[x];
            currentSurfaceIds[index] = currentSurface;
            ++stats.active_pixels;

            bool valid = false;
            bool outOfBounds = false;
            std::uint32_t previousSurface = 0u;
            if (previousSurfaceAvailable) {
                const float previousX = static_cast<float>(x) + 0.5f + motionRow[x].x + jitterDeltaX;
                const float previousY = static_cast<float>(y) + 0.5f + motionRow[x].y + jitterDeltaY;
                const int previousPixelX = static_cast<int>(std::floor(previousX));
                const int previousPixelY = static_cast<int>(std::floor(previousY));
                outOfBounds = previousPixelX < 0 || previousPixelY < 0 ||
                              previousPixelX >= static_cast<int>(g_width) || previousPixelY >= static_cast<int>(g_height);
                if (!outOfBounds) {
                    const std::size_t previousIndex = static_cast<std::size_t>(previousPixelY) * g_width +
                                                      static_cast<std::size_t>(previousPixelX);
                    previousSurface = g_previous_surface_ids[previousIndex];
                    valid = currentSurface != 0u && previousSurface == currentSurface;
                }
            }

            if (valid) {
                ++stats.valid_pixels;
                if (diagnosticLabel) diagnosticPixels[index] = PackBgra(0.0f, 1.0f, 0.0f);
            } else {
                ++stats.invalid_pixels;
                if (outOfBounds) {
                    ++stats.out_of_bounds_pixels;
                    if (diagnosticLabel) diagnosticPixels[index] = PackBgra(1.0f, 1.0f, 0.0f);
                } else if (previousSurfaceAvailable) {
                    ++stats.surface_mismatch_pixels;
                    if (currentSurface == 1u && previousSurface == 2u) ++stats.disoccluded_background_pixels;
                    if (diagnosticLabel) diagnosticPixels[index] = PackBgra(1.0f, 0.0f, 0.0f);
                } else if (diagnosticLabel) {
                    diagnosticPixels[index] = PackBgra(0.25f, 0.25f, 0.25f);
                }
            }
        }
    }

    if (diagnosticLabel) {
        WriteBitmap32(std::string("ltr_diag_") + diagnosticLabel + "_history_validity.bmp", diagnosticPixels);
    }
    g_previous_surface_ids = std::move(currentSurfaceIds);

    g_context->Unmap(surfaceReadback.Get(), 0);
    g_context->Unmap(motionReadback.Get(), 0);
    g_context->Unmap(depthReadback.Get(), 0);
    return stats;
}

ReconstructionStats ReadbackReconstructionComparison(const char* diagnosticLabel = nullptr) {
    const auto depthReadback = CreateReadbackTexture(g_depth.Get());
    const auto motionReadback = CreateReadbackTexture(g_motion.Get());
    const auto reconstructedReadback = CreateReadbackTexture(g_reconstructed_motion.Get());
    g_context->CopyResource(depthReadback.Get(), g_depth.Get());
    g_context->CopyResource(motionReadback.Get(), g_motion.Get());
    g_context->CopyResource(reconstructedReadback.Get(), g_reconstructed_motion.Get());

    D3D11_MAPPED_SUBRESOURCE depthMapped{};
    D3D11_MAPPED_SUBRESOURCE motionMapped{};
    D3D11_MAPPED_SUBRESOURCE reconstructedMapped{};
    CheckHr(g_context->Map(depthReadback.Get(), 0, D3D11_MAP_READ, 0, &depthMapped), "Map(depth comparison)");
    HRESULT hr = g_context->Map(motionReadback.Get(), 0, D3D11_MAP_READ, 0, &motionMapped);
    if (FAILED(hr)) {
        g_context->Unmap(depthReadback.Get(), 0);
        ThrowHr("Map(motion comparison)", hr);
    }
    hr = g_context->Map(reconstructedReadback.Get(), 0, D3D11_MAP_READ, 0, &reconstructedMapped);
    if (FAILED(hr)) {
        g_context->Unmap(motionReadback.Get(), 0);
        g_context->Unmap(depthReadback.Get(), 0);
        ThrowHr("Map(reconstructed comparison)", hr);
    }

    ReconstructionStats stats{};
    double errorSum = 0.0;
    double reconstructedSum = 0.0;
    std::vector<std::uint32_t> reconstructedPixels;
    std::vector<std::uint32_t> errorPixels;
    if (diagnosticLabel) {
        reconstructedPixels.resize(static_cast<std::size_t>(g_width) * g_height);
        errorPixels.resize(static_cast<std::size_t>(g_width) * g_height);
    }

    for (std::uint32_t y = 0; y < g_height; ++y) {
        const auto* depthRow = reinterpret_cast<const float*>(static_cast<const std::byte*>(depthMapped.pData) + y * depthMapped.RowPitch);
        const auto* motionRow = reinterpret_cast<const XMFLOAT2*>(static_cast<const std::byte*>(motionMapped.pData) + y * motionMapped.RowPitch);
        const auto* reconstructedRow = reinterpret_cast<const XMFLOAT2*>(static_cast<const std::byte*>(reconstructedMapped.pData) + y * reconstructedMapped.RowPitch);
        for (std::uint32_t x = 0; x < g_width; ++x) {
            if (!std::isfinite(depthRow[x]) || depthRow[x] >= 0.99999f) continue;
            ++stats.active_pixels;
            const float gtMagnitude = std::hypot(motionRow[x].x, motionRow[x].y);
            const float reconstructedMagnitude = std::hypot(reconstructedRow[x].x, reconstructedRow[x].y);
            const float error = std::hypot(reconstructedRow[x].x - motionRow[x].x,
                                           reconstructedRow[x].y - motionRow[x].y);
            if (!std::isfinite(gtMagnitude) || !std::isfinite(reconstructedMagnitude) || !std::isfinite(error)) {
                g_context->Unmap(reconstructedReadback.Get(), 0);
                g_context->Unmap(motionReadback.Get(), 0);
                g_context->Unmap(depthReadback.Get(), 0);
                throw std::runtime_error("non-finite value found during reconstruction comparison");
            }

            if (gtMagnitude > 0.05f) ++stats.ground_truth_moving_pixels;
            if (reconstructedMagnitude > 0.05f) ++stats.reconstructed_moving_pixels;
            if (error <= 0.5f) ++stats.close_pixels;
            if (gtMagnitude > 0.25f && reconstructedMagnitude <= 0.05f) ++stats.camera_only_miss_pixels;
            stats.max_error = (std::max)(stats.max_error, error);
            stats.max_reconstructed_motion = (std::max)(stats.max_reconstructed_motion, reconstructedMagnitude);
            errorSum += error;
            reconstructedSum += reconstructedMagnitude;

            if (diagnosticLabel) {
                const std::size_t index = static_cast<std::size_t>(y) * g_width + x;
                const float motionX = (std::clamp)(reconstructedRow[x].x / 128.0f, -1.0f, 1.0f);
                const float motionY = (std::clamp)(reconstructedRow[x].y / 128.0f, -1.0f, 1.0f);
                reconstructedPixels[index] = PackBgra(0.5f + 0.5f * motionX, 0.5f + 0.5f * motionY,
                                                       (std::min)(reconstructedMagnitude / 128.0f, 1.0f));
                const float errorVisual = (std::min)(error / 4.0f, 1.0f);
                errorPixels[index] = PackBgra(errorVisual, 0.0f, 1.0f - errorVisual);
            }
        }
    }

    if (stats.active_pixels != 0) {
        stats.mean_error = errorSum / static_cast<double>(stats.active_pixels);
        stats.mean_reconstructed_motion = reconstructedSum / static_cast<double>(stats.active_pixels);
    }
    if (diagnosticLabel) {
        const std::string prefix = std::string("ltr_diag_") + diagnosticLabel;
        WriteBitmap32(prefix + "_reconstructed_motion.bmp", reconstructedPixels);
        WriteBitmap32(prefix + "_reconstruction_error.bmp", errorPixels);
    }

    g_context->Unmap(reconstructedReadback.Get(), 0);
    g_context->Unmap(motionReadback.Get(), 0);
    g_context->Unmap(depthReadback.Get(), 0);
    return stats;
}

const wchar_t* ScenarioName() {
    switch (g_scenario) {
    case Scenario::static_scene: return L"static"; case Scenario::camera_translate: return L"camera-translate";
    case Scenario::camera_rotate: return L"camera-rotate"; case Scenario::rigid_object: return L"rigid-object";
    case Scenario::disocclusion: return L"disocclusion"; default: return L"unknown";
    }
}

const wchar_t* ViewName() {
    switch (g_debug_view) {
    case DebugView::scene: return L"scene";
    case DebugView::depth: return L"depth";
    case DebugView::motion: return L"motion";
    case DebugView::reconstructed_motion: return L"reconstructed-motion";
    default: return L"unknown";
    }
}

void UpdateTitle() {
    wchar_t title[384]{};
    swprintf_s(title, L"LTR Bridge | scenario=%s | view=%s | frame=%llu | history=%u | jitter=(%.3f, %.3f) px | 1-5 scenario, Tab debug, R reset",
               ScenarioName(), ViewName(), static_cast<unsigned long long>(g_last_frame.identity.frame_index),
               g_last_frame.identity.history_generation, g_last_frame.jitter_x_pixels, g_last_frame.jitter_y_pixels);
    SetWindowTextW(g_window, title);
}

void Render(double seconds) {
    if (g_pending_width && g_pending_height) {
        if (g_pending_width != g_width || g_pending_height != g_height) {
            CreateFrameResources(g_pending_width, g_pending_height);
            g_pending_reset = ltr::harness::HistoryResetReason::resize;
        }
        g_pending_width = g_pending_height = 0;
    }

    const float t = static_cast<float>(seconds);
    XMVECTOR eye = XMVectorSet(0.0f, 1.4f, -5.0f, 1.0f);
    XMVECTOR target = XMVectorZero();
    XMMATRIX world = XMMatrixIdentity();
    if (g_scenario == Scenario::camera_translate) {
        const float x = std::sin(t * 0.75f) * 1.25f; eye = XMVectorSet(x, 1.4f, -5.0f, 1.0f); target = XMVectorSet(x * 0.2f, 0.0f, 0.0f, 1.0f);
    } else if (g_scenario == Scenario::camera_rotate) {
        const float a = std::sin(t * 0.45f) * 0.55f; eye = XMVectorSet(std::sin(a) * 5.0f, 1.4f, -std::cos(a) * 5.0f, 1.0f);
    } else if (g_scenario == Scenario::rigid_object) {
        world = XMMatrixRotationY(t) * XMMatrixTranslation(std::sin(t) * 1.5f, 0.0f, 0.0f);
    } else if (g_scenario == Scenario::disocclusion) {
        world = XMMatrixTranslation(-1.6f + 3.2f * t, 0.0f, 0.0f);
    }

    const XMMATRIX view = XMMatrixLookAtLH(eye, target, XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f));
    const XMMATRIX projection = XMMatrixPerspectiveFovLH(XMConvertToRadians(60.0f), static_cast<float>(g_width) / g_height, 0.1f, 100.0f);
    const std::uint64_t jitterIndex = (g_frame_index % 8u) + 1u;
    const float jitterX = Halton(jitterIndex, 2) - 0.5f, jitterY = Halton(jitterIndex, 3) - 0.5f;
    const XMMATRIX vp = view * projection;
    const XMMATRIX jitteredVp = view * JitterProjection(projection, jitterX, jitterY);

    g_last_frame = {};
    g_last_frame.identity.frame_index = g_frame_index; g_last_frame.identity.view_index = 0;
    g_last_frame.identity.history_generation = g_history_generation; g_last_frame.identity.reset_reason = g_pending_reset;
    if (g_pending_reset != ltr::harness::HistoryResetReason::none) g_last_frame.identity.history_generation = ++g_history_generation;
    g_last_frame.render_width = g_last_frame.output_width = g_width; g_last_frame.render_height = g_last_frame.output_height = g_height;
    g_last_frame.jitter_x_pixels = jitterX; g_last_frame.jitter_y_pixels = jitterY;
    g_last_frame.motion.coverage = ltr::harness::coverage_camera | ltr::harness::coverage_rigid_objects;
    g_last_frame.motion.known_exclusions = ltr::harness::coverage_skinned_geometry | ltr::harness::coverage_particles | ltr::harness::coverage_transparency | ltr::harness::coverage_hud;
    g_last_frame.motion.jitter_included = false;

    XMMATRIX previousWorld = g_previous_world, previousVp = g_previous_vp;
    g_last_previous_jitter_x = g_previous_jitter_x;
    g_last_previous_jitter_y = g_previous_jitter_y;
    if (!g_previous_valid || g_pending_reset != ltr::harness::HistoryResetReason::none) {
        previousWorld = world; previousVp = vp;
        g_last_previous_jitter_x = jitterX;
        g_last_previous_jitter_y = jitterY;
    }

    const XMVECTOR probeLocal = XMVectorSet(0.7f, 0.5f, 0.0f, 1.0f);
    const XMVECTOR currentProbeClip = XMVector4Transform(XMVector4Transform(probeLocal, world), vp);
    const XMVECTOR previousProbeClip = XMVector4Transform(XMVector4Transform(probeLocal, previousWorld), previousVp);
    const float currentProbeX = (XMVectorGetX(currentProbeClip) / XMVectorGetW(currentProbeClip) * 0.5f + 0.5f) * g_width;
    const float currentProbeY = (-XMVectorGetY(currentProbeClip) / XMVectorGetW(currentProbeClip) * 0.5f + 0.5f) * g_height;
    const float previousProbeX = (XMVectorGetX(previousProbeClip) / XMVectorGetW(previousProbeClip) * 0.5f + 0.5f) * g_width;
    const float previousProbeY = (-XMVectorGetY(previousProbeClip) / XMVectorGetW(previousProbeClip) * 0.5f + 0.5f) * g_height;
    g_cpu_probe_motion = std::hypot(previousProbeX - currentProbeX, previousProbeY - currentProbeY);

    const float sceneClear[4]{0.035f, 0.045f, 0.065f, 1.0f}; const float motionClear[4]{};
    g_context->ClearRenderTargetView(g_scene_color_rtv.Get(), sceneClear); g_context->ClearRenderTargetView(g_motion_rtv.Get(), motionClear);
    g_context->ClearDepthStencilView(g_depth_dsv.Get(), D3D11_CLEAR_DEPTH, 1.0f, 0);
    ID3D11RenderTargetView* sceneTargets[3]{g_scene_color_rtv.Get(), g_motion_rtv.Get(), g_surface_id_rtv.Get()};
    g_context->OMSetRenderTargets(3, sceneTargets, g_depth_dsv.Get());
    D3D11_VIEWPORT viewport{0.0f, 0.0f, static_cast<float>(g_width), static_cast<float>(g_height), 0.0f, 1.0f}; g_context->RSSetViewports(1, &viewport);
    const UINT stride = sizeof(Vertex), offset = 0; ID3D11Buffer* vb = g_vertex_buffer.Get();
    g_context->IASetInputLayout(g_input_layout.Get()); g_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    g_context->IASetVertexBuffers(0, 1, &vb, &stride, &offset); g_context->VSSetShader(g_scene_vs.Get(), nullptr, 0); g_context->PSSetShader(g_scene_ps.Get(), nullptr, 0);
    UploadPerDraw(world, previousWorld, jitteredVp, vp, previousVp, 2u); g_context->Draw(3, 0);
    UploadPerDraw(XMMatrixIdentity(), XMMatrixIdentity(), jitteredVp, vp, previousVp, 1u); g_context->Draw(3, 3);

    g_context->OMSetRenderTargets(0, nullptr, nullptr);
    const float reconstructedClear[4]{};
    g_context->ClearRenderTargetView(g_reconstructed_motion_rtv.Get(), reconstructedClear);
    ID3D11RenderTargetView* reconstructedTarget = g_reconstructed_motion_rtv.Get();
    g_context->OMSetRenderTargets(1, &reconstructedTarget, nullptr);
    g_context->IASetInputLayout(nullptr);
    g_context->VSSetShader(g_debug_vs.Get(), nullptr, 0);
    g_context->PSSetShader(g_reconstruct_ps.Get(), nullptr, 0);
    ReconstructConstants reconstruct{};
    const XMMATRIX inverseCurrentJittered = XMMatrixInverse(nullptr, jitteredVp);
    XMStoreFloat4x4(&reconstruct.inverse_current_vp_jittered, inverseCurrentJittered);
    XMStoreFloat4x4(&reconstruct.current_vp_unjittered, vp);
    XMStoreFloat4x4(&reconstruct.previous_vp_unjittered, previousVp);
    reconstruct.render_size = { static_cast<float>(g_width), static_cast<float>(g_height) };
    D3D11_MAPPED_SUBRESOURCE reconstructMapped{};
    CheckHr(g_context->Map(g_reconstruct_buffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &reconstructMapped), "Map(reconstruct)");
    std::memcpy(reconstructMapped.pData, &reconstruct, sizeof(reconstruct));
    g_context->Unmap(g_reconstruct_buffer.Get(), 0);
    ID3D11Buffer* reconstructBuffer = g_reconstruct_buffer.Get();
    g_context->PSSetConstantBuffers(0, 1, &reconstructBuffer);
    ID3D11ShaderResourceView* reconstructResources[1]{g_depth_srv.Get()};
    g_context->PSSetShaderResources(0, 1, reconstructResources);
    g_context->Draw(3, 0);
    ID3D11ShaderResourceView* nullReconstructResources[1]{};
    g_context->PSSetShaderResources(0, 1, nullReconstructResources);

    g_context->OMSetRenderTargets(0, nullptr, nullptr); ID3D11RenderTargetView* backbuffer = g_backbuffer_rtv.Get(); g_context->OMSetRenderTargets(1, &backbuffer, nullptr);
    g_context->IASetInputLayout(nullptr); g_context->VSSetShader(g_debug_vs.Get(), nullptr, 0); g_context->PSSetShader(g_debug_ps.Get(), nullptr, 0);
    DebugConstants debug{}; debug.mode = static_cast<std::uint32_t>(g_debug_view);
    D3D11_MAPPED_SUBRESOURCE mapped{}; CheckHr(g_context->Map(g_debug_buffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped), "Map(debug)");
    std::memcpy(mapped.pData, &debug, sizeof(debug)); g_context->Unmap(g_debug_buffer.Get(), 0);
    ID3D11Buffer* debugBuffer = g_debug_buffer.Get(); g_context->PSSetConstantBuffers(0, 1, &debugBuffer);
    ID3D11ShaderResourceView* resources[4]{g_scene_color_srv.Get(), g_depth_srv.Get(), g_motion_srv.Get(), g_reconstructed_motion_srv.Get()}; g_context->PSSetShaderResources(0, 4, resources);
    ID3D11SamplerState* sampler = g_sampler.Get(); g_context->PSSetSamplers(0, 1, &sampler); g_context->Draw(3, 0);
    ID3D11ShaderResourceView* nullSrvs[4]{}; g_context->PSSetShaderResources(0, 4, nullSrvs);
    CheckHr(g_swap_chain->Present(g_self_test ? 0u : 1u, 0), "Present");

    g_previous_world = world; g_previous_vp = vp;
    g_previous_jitter_x = jitterX; g_previous_jitter_y = jitterY;
    g_previous_valid = true; g_pending_reset = ltr::harness::HistoryResetReason::none;
    ++g_frame_index; UpdateTitle();
}

bool HasExpectedMotion(const ReadbackStats& stats, bool expectMotion) {
    if (expectMotion) return stats.max_motion > 0.25f && stats.moving_pixels > 0;
    return stats.max_motion <= 0.05f && stats.moving_pixels == 0;
}

void AppendStats(std::ostringstream& report, const char* label, const char* phase, const ReadbackStats& stats, bool ok) {
    report << (ok ? "PASS " : "FAIL ") << label << ' ' << phase
           << " active=" << stats.active_pixels
           << " moving=" << stats.moving_pixels
           << " min_depth=" << stats.min_depth
           << " mean_motion=" << stats.mean_motion
           << " max_motion=" << stats.max_motion
           << " cpu_probe_motion=" << g_cpu_probe_motion << '\n';
}

void AppendReconstructionStats(std::ostringstream& report, const char* label, const char* phase,
                               const ReconstructionStats& stats, bool ok) {
    report << (ok ? "PASS " : "FAIL ") << label << ' ' << phase << " camera-depth"
           << " active=" << stats.active_pixels
           << " gt_moving=" << stats.ground_truth_moving_pixels
           << " reconstructed_moving=" << stats.reconstructed_moving_pixels
           << " close_0.5px=" << stats.close_pixels
           << " camera_only_miss=" << stats.camera_only_miss_pixels
           << " mean_reconstructed_motion=" << stats.mean_reconstructed_motion
           << " max_reconstructed_motion=" << stats.max_reconstructed_motion
           << " mean_error=" << stats.mean_error
           << " max_error=" << stats.max_error << '\n';
}

void AppendHistoryValidityStats(std::ostringstream& report, const char* label, const char* phase,
                                const HistoryValidityStats& stats, bool ok) {
    report << (ok ? "PASS " : "FAIL ") << label << ' ' << phase << " history-validity"
           << " active=" << stats.active_pixels
           << " valid=" << stats.valid_pixels
           << " invalid=" << stats.invalid_pixels
           << " mismatch=" << stats.surface_mismatch_pixels
           << " out_of_bounds=" << stats.out_of_bounds_pixels
           << " disoccluded_background=" << stats.disoccluded_background_pixels << '\n';
}

bool ValidateHistoryValidity(const HistoryValidityStats& stats, bool historyAvailable, Scenario scenario) {
    const std::uint64_t minimumActive = static_cast<std::uint64_t>(g_width) * g_height / 100u;
    if (stats.active_pixels < minimumActive) return false;
    if (stats.valid_pixels + stats.invalid_pixels != stats.active_pixels) return false;

    if (!historyAvailable) {
        return stats.valid_pixels == 0 && stats.invalid_pixels == stats.active_pixels &&
               stats.surface_mismatch_pixels == 0 && stats.out_of_bounds_pixels == 0 &&
               stats.disoccluded_background_pixels == 0;
    }

    if (stats.surface_mismatch_pixels + stats.out_of_bounds_pixels != stats.invalid_pixels) return false;
    if (scenario == Scenario::static_scene) {
        return stats.valid_pixels >= (stats.active_pixels * 999u) / 1000u &&
               stats.disoccluded_background_pixels <= stats.active_pixels / 1000u;
    }
    if (scenario == Scenario::rigid_object || scenario == Scenario::disocclusion) {
        return stats.valid_pixels >= (stats.active_pixels * 9u) / 10u &&
               stats.disoccluded_background_pixels > stats.active_pixels / 100u;
    }
    return stats.valid_pixels >= (stats.active_pixels * 95u) / 100u;
}

bool ValidateReconstruction(const ReconstructionStats& stats, bool expectCameraMotion,
                            bool expectCameraOnlyLimitation) {
    const std::uint64_t minimumActive = static_cast<std::uint64_t>(g_width) * g_height / 100u;
    if (stats.active_pixels < minimumActive) return false;

    if (expectCameraOnlyLimitation) {
        return stats.ground_truth_moving_pixels > stats.active_pixels / 100u &&
               stats.reconstructed_moving_pixels == 0 &&
               stats.max_reconstructed_motion <= 0.01f &&
               stats.camera_only_miss_pixels >= (stats.ground_truth_moving_pixels * 99u) / 100u &&
               stats.close_pixels < stats.active_pixels;
    }

    if (expectCameraMotion) {
        return stats.ground_truth_moving_pixels > (stats.active_pixels * 9u) / 10u &&
               stats.reconstructed_moving_pixels > (stats.active_pixels * 9u) / 10u &&
               stats.close_pixels >= (stats.active_pixels * 99u) / 100u &&
               stats.camera_only_miss_pixels == 0 &&
               stats.mean_error <= 0.05 && stats.max_error <= 0.1f;
    }

    return stats.ground_truth_moving_pixels == 0 && stats.reconstructed_moving_pixels == 0 &&
           stats.camera_only_miss_pixels == 0 && stats.close_pixels == stats.active_pixels &&
           stats.max_reconstructed_motion <= 0.01f && stats.mean_error <= 0.01 && stats.max_error <= 0.01f;
}

bool ValidateStats(const ReadbackStats& stats, bool expectMotion, bool requireStaticCoverage) {
    const std::uint64_t minimumActive = static_cast<std::uint64_t>(g_width) * g_height / 100u;
    if (stats.active_pixels < minimumActive) return false;
    if (!(stats.min_depth >= 0.0f && stats.min_depth < 0.99999f)) return false;
    if (!HasExpectedMotion(stats, expectMotion)) return false;
    if (requireStaticCoverage && stats.moving_pixels >= (stats.active_pixels * 4u) / 5u) return false;
    return true;
}

bool RunScenarioCheck(Scenario scenario, const char* name, double secondTime,
                      bool expectMotion, bool requireStaticCoverage, std::ostringstream& report) {
    g_scenario = scenario;
    g_pending_reset = ltr::harness::HistoryResetReason::scenario_change;
    const bool expectCameraMotion = scenario == Scenario::camera_translate || scenario == Scenario::camera_rotate;
    const bool expectCameraOnlyLimitation = scenario == Scenario::rigid_object || scenario == Scenario::disocclusion;
    const std::uint32_t expectedGeneration = g_history_generation + 1u;

    Render(0.0);
    const ReadbackStats resetStats = ReadbackGroundTruth();
    const ReconstructionStats resetReconstructionStats = ReadbackReconstructionComparison();
    const HistoryValidityStats resetHistoryStats = ReadbackHistoryValidity(false);
    const bool resetReconstructionOk = ValidateReconstruction(resetReconstructionStats, false, false);
    const bool resetHistoryOk = ValidateHistoryValidity(resetHistoryStats, false, scenario);
    const bool resetOk = g_last_frame.identity.history_generation == expectedGeneration &&
                         g_last_frame.identity.reset_reason == ltr::harness::HistoryResetReason::scenario_change &&
                         ValidateStats(resetStats, false, false) && resetReconstructionOk && resetHistoryOk;
    AppendStats(report, name, "reset", resetStats, resetOk);
    AppendReconstructionStats(report, name, "reset", resetReconstructionStats, resetReconstructionOk);
    AppendHistoryValidityStats(report, name, "reset", resetHistoryStats, resetHistoryOk);

    const std::uint64_t expectedFrame = g_last_frame.identity.frame_index + 1u;
    Render(secondTime);
    const ReadbackStats steadyStats = ReadbackGroundTruth(name);
    const ReconstructionStats steadyReconstructionStats = ReadbackReconstructionComparison(name);
    const HistoryValidityStats steadyHistoryStats = ReadbackHistoryValidity(true, name);
    const bool steadyReconstructionOk =
        ValidateReconstruction(steadyReconstructionStats, expectCameraMotion, expectCameraOnlyLimitation);
    const bool steadyHistoryOk = ValidateHistoryValidity(steadyHistoryStats, true, scenario);
    const bool steadyOk = g_last_frame.identity.frame_index == expectedFrame &&
                          g_last_frame.identity.history_generation == expectedGeneration &&
                          g_last_frame.identity.reset_reason == ltr::harness::HistoryResetReason::none &&
                          ValidateStats(steadyStats, expectMotion, requireStaticCoverage) && steadyReconstructionOk && steadyHistoryOk;
    AppendStats(report, name, "steady", steadyStats, steadyOk);
    AppendReconstructionStats(report, name, "steady", steadyReconstructionStats, steadyReconstructionOk);
    AppendHistoryValidityStats(report, name, "steady", steadyHistoryStats, steadyHistoryOk);
    return resetOk && steadyOk;
}

bool RunDeterministicSelfTest() {
    std::ostringstream report;
    report << "LTR Bridge deterministic D3D11 temporal readback\n";
    report << "extent=" << g_width << 'x' << g_height
           << " motion=R32G32_FLOAT direction=current-to-previous units=render-pixels jitter-included=false\n";

    bool ok = true;
    ok &= RunScenarioCheck(Scenario::static_scene, "static", 0.0, false, false, report);
    ok &= RunScenarioCheck(Scenario::camera_translate, "camera-translate", 1.0, true, false, report);
    ok &= RunScenarioCheck(Scenario::camera_rotate, "camera-rotate", 1.0, true, false, report);
    ok &= RunScenarioCheck(Scenario::rigid_object, "rigid-object", 1.0, true, true, report);
    ok &= RunScenarioCheck(Scenario::disocclusion, "disocclusion", 1.0, true, true, report);
    report << (ok ? "RESULT PASS\n" : "RESULT FAIL\n");

    std::ofstream output("ltr_harness_selftest.txt", std::ios::trunc);
    output << report.str();
    output.close();
    OutputDebugStringA(report.str().c_str());
    return ok;
}

LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_SIZE:
        if (wParam != SIZE_MINIMIZED) { g_pending_width = LOWORD(lParam); g_pending_height = HIWORD(lParam); }
        return 0;
    case WM_KEYDOWN:
        if (wParam == VK_ESCAPE) DestroyWindow(window);
        else if (wParam == VK_TAB) g_debug_view = static_cast<DebugView>((static_cast<std::uint32_t>(g_debug_view) + 1u) % static_cast<std::uint32_t>(DebugView::count));
        else if (wParam == 'R') g_pending_reset = ltr::harness::HistoryResetReason::manual;
        else if (wParam >= '1' && wParam <= '5') { g_scenario = static_cast<Scenario>(wParam - '1'); g_pending_reset = ltr::harness::HistoryResetReason::scenario_change; }
        return 0;
    case WM_DESTROY: PostQuitMessage(0); return 0;
    default: return DefWindowProcW(window, message, wParam, lParam);
    }
}

} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int showCommand) {
    try {
        g_self_test = std::wcsstr(GetCommandLineW(), L"--self-test") != nullptr;
        WNDCLASSEXW wc{}; wc.cbSize = sizeof(wc); wc.style = CS_HREDRAW | CS_VREDRAW; wc.lpfnWndProc = WindowProc;
        wc.hInstance = instance; wc.hCursor = LoadCursorW(nullptr, IDC_ARROW); wc.lpszClassName = kWindowClass;
        if (!RegisterClassExW(&wc)) ThrowHr("RegisterClassExW", HRESULT_FROM_WIN32(GetLastError()));
        RECT rect{0, 0, static_cast<LONG>(g_width), static_cast<LONG>(g_height)}; AdjustWindowRect(&rect, WS_OVERLAPPEDWINDOW, FALSE);
        g_window = CreateWindowExW(0, kWindowClass, kWindowTitle, WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
                                   rect.right - rect.left, rect.bottom - rect.top, nullptr, nullptr, instance, nullptr);
        if (!g_window) ThrowHr("CreateWindowExW", HRESULT_FROM_WIN32(GetLastError()));
        if (!g_self_test) ShowWindow(g_window, showCommand);
        CreateDeviceAndPipeline();
        if (g_self_test) {
            const bool ok = RunDeterministicSelfTest();
            DestroyWindow(g_window);
            return ok ? 0 : 2;
        }
        const auto start = std::chrono::steady_clock::now(); MSG msg{};
        while (msg.message != WM_QUIT) {
            if (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) { TranslateMessage(&msg); DispatchMessageW(&msg); }
            else Render(std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count());
        }
        return static_cast<int>(msg.wParam);
    } catch (const std::exception& error) {
        MessageBoxA(nullptr, error.what(), "LTR Bridge temporal harness", MB_OK | MB_ICONERROR); return 1;
    }
}
