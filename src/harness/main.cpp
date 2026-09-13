#include "temporal_frame.h"

#include <Windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <DirectXMath.h>
#include <wrl/client.h>

#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string_view>

using Microsoft::WRL::ComPtr;
using namespace DirectX;

namespace {

constexpr wchar_t kWindowClass[] = L"LTRBridgeTemporalHarness";
constexpr wchar_t kWindowTitle[] = L"LTR Bridge - D3D11 x64 Temporal Harness";

enum class Scenario : std::uint32_t { static_scene = 0, camera_translate, camera_rotate, rigid_object, count };
enum class DebugView : std::uint32_t { scene = 0, depth, motion, count };

struct Vertex { XMFLOAT3 position; XMFLOAT3 color; };

struct PerDrawConstants {
    XMFLOAT4X4 current_world;
    XMFLOAT4X4 previous_world;
    XMFLOAT4X4 current_vp_jittered;
    XMFLOAT4X4 current_vp_unjittered;
    XMFLOAT4X4 previous_vp_unjittered;
    XMFLOAT2 render_size;
    XMFLOAT2 padding{};
};

struct DebugConstants {
    std::uint32_t mode = 0;
    float motion_scale = 16.0f;
    float padding[2]{};
};

static_assert(sizeof(PerDrawConstants) % 16 == 0);
static_assert(sizeof(DebugConstants) % 16 == 0);

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
XMMATRIX g_previous_world = XMMatrixIdentity();
XMMATRIX g_previous_vp = XMMatrixIdentity();
ltr::harness::TemporalFrameDescription g_last_frame{};

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
ComPtr<ID3D11Texture2D> g_depth;
ComPtr<ID3D11DepthStencilView> g_depth_dsv;
ComPtr<ID3D11ShaderResourceView> g_depth_srv;
ComPtr<ID3D11VertexShader> g_scene_vs;
ComPtr<ID3D11PixelShader> g_scene_ps;
ComPtr<ID3D11VertexShader> g_debug_vs;
ComPtr<ID3D11PixelShader> g_debug_ps;
ComPtr<ID3D11InputLayout> g_input_layout;
ComPtr<ID3D11Buffer> g_vertex_buffer;
ComPtr<ID3D11Buffer> g_per_draw_buffer;
ComPtr<ID3D11Buffer> g_debug_buffer;
ComPtr<ID3D11SamplerState> g_sampler;

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
    float2 padding;
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
struct PSOut { float4 color : SV_Target0; float2 motion : SV_Target1; };
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
    return output;
}
)hlsl";

constexpr std::string_view kDebugShader = R"hlsl(
Texture2D<float4> sceneTexture : register(t0);
Texture2D<float> depthTexture : register(t1);
Texture2D<float2> motionTexture : register(t2);
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
    float2 motion = motionTexture.Sample(linearClamp, input.uv);
    float2 signedMotion = clamp(motion / motionScale, -1.0, 1.0);
    float magnitude = saturate(length(motion) / motionScale);
    return float4(0.5 + 0.5 * signedMotion.x, 0.5 + 0.5 * signedMotion.y, magnitude, 1.0);
}
)hlsl";

void CreateFrameResources(std::uint32_t width, std::uint32_t height) {
    ID3D11ShaderResourceView* nullSrvs[3]{};
    g_context->PSSetShaderResources(0, 3, nullSrvs);
    g_context->OMSetRenderTargets(0, nullptr, nullptr);
    g_backbuffer_rtv.Reset();
    g_scene_color.Reset(); g_scene_color_rtv.Reset(); g_scene_color_srv.Reset();
    g_motion.Reset(); g_motion_rtv.Reset(); g_motion_srv.Reset();
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

    color.Format = DXGI_FORMAT_R16G16_FLOAT;
    CheckHr(g_device->CreateTexture2D(&color, nullptr, &g_motion), "CreateTexture2D(motion)");
    CheckHr(g_device->CreateRenderTargetView(g_motion.Get(), nullptr, &g_motion_rtv), "CreateRTV(motion)");
    CheckHr(g_device->CreateShaderResourceView(g_motion.Get(), nullptr, &g_motion_srv), "CreateSRV(motion)");

    D3D11_TEXTURE2D_DESC depth{};
    depth.Width = width; depth.Height = height; depth.MipLevels = 1; depth.ArraySize = 1;
    depth.Format = DXGI_FORMAT_R32_TYPELESS; depth.SampleDesc.Count = 1;
    depth.Usage = D3D11_USAGE_DEFAULT; depth.BindFlags = D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE;
    CheckHr(g_device->CreateTexture2D(&depth, nullptr, &g_depth), "CreateTexture2D(depth)");
    D3D11_DEPTH_STENCIL_VIEW_DESC dsv{}; dsv.Format = DXGI_FORMAT_D32_FLOAT; dsv.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
    CheckHr(g_device->CreateDepthStencilView(g_depth.Get(), &dsv, &g_depth_dsv), "CreateDSV(depth)");
    D3D11_SHADER_RESOURCE_VIEW_DESC srv{}; srv.Format = DXGI_FORMAT_R32_FLOAT; srv.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D; srv.Texture2D.MipLevels = 1;
    CheckHr(g_device->CreateShaderResourceView(g_depth.Get(), &srv, &g_depth_srv), "CreateSRV(depth)");

    g_width = width; g_height = height; g_previous_valid = false;
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
    CheckHr(g_device->CreateVertexShader(sceneVs->GetBufferPointer(), sceneVs->GetBufferSize(), nullptr, &g_scene_vs), "CreateVertexShader");
    CheckHr(g_device->CreatePixelShader(scenePs->GetBufferPointer(), scenePs->GetBufferSize(), nullptr, &g_scene_ps), "CreatePixelShader");
    CheckHr(g_device->CreateVertexShader(debugVs->GetBufferPointer(), debugVs->GetBufferSize(), nullptr, &g_debug_vs), "CreateDebugVS");
    CheckHr(g_device->CreatePixelShader(debugPs->GetBufferPointer(), debugPs->GetBufferSize(), nullptr, &g_debug_ps), "CreateDebugPS");

    const D3D11_INPUT_ELEMENT_DESC elements[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, static_cast<UINT>(offsetof(Vertex, position)), D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "COLOR", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, static_cast<UINT>(offsetof(Vertex, color)), D3D11_INPUT_PER_VERTEX_DATA, 0 },
    };
    CheckHr(g_device->CreateInputLayout(elements, 2, sceneVs->GetBufferPointer(), sceneVs->GetBufferSize(), &g_input_layout), "CreateInputLayout");

    constexpr std::array<Vertex, 6> vertices{{
        {{-1.1f, -0.8f, 0.0f}, {0.9f, 0.2f, 0.2f}}, {{0.0f, 1.0f, 0.0f}, {0.2f, 0.9f, 0.3f}}, {{1.1f, -0.8f, 0.0f}, {0.2f, 0.3f, 0.95f}},
        {{-4.0f, -1.4f, -2.0f}, {0.25f, 0.27f, 0.31f}}, {{4.0f, -1.4f, -2.0f}, {0.32f, 0.34f, 0.38f}}, {{0.0f, -1.4f, 5.0f}, {0.20f, 0.22f, 0.26f}},
    }};
    D3D11_BUFFER_DESC vb{}; vb.ByteWidth = sizeof(vertices); vb.Usage = D3D11_USAGE_IMMUTABLE; vb.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    D3D11_SUBRESOURCE_DATA init{}; init.pSysMem = vertices.data();
    CheckHr(g_device->CreateBuffer(&vb, &init, &g_vertex_buffer), "CreateBuffer(vertices)");

    D3D11_BUFFER_DESC cb{}; cb.ByteWidth = sizeof(PerDrawConstants); cb.Usage = D3D11_USAGE_DYNAMIC; cb.BindFlags = D3D11_BIND_CONSTANT_BUFFER; cb.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    CheckHr(g_device->CreateBuffer(&cb, nullptr, &g_per_draw_buffer), "CreateBuffer(per-draw)");
    cb.ByteWidth = sizeof(DebugConstants);
    CheckHr(g_device->CreateBuffer(&cb, nullptr, &g_debug_buffer), "CreateBuffer(debug)");

    D3D11_SAMPLER_DESC sampler{}; sampler.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sampler.AddressU = sampler.AddressV = sampler.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP; sampler.MaxLOD = D3D11_FLOAT32_MAX;
    CheckHr(g_device->CreateSamplerState(&sampler, &g_sampler), "CreateSamplerState");
    CreateFrameResources(g_width, g_height);
}

void UploadPerDraw(const XMMATRIX& world, const XMMATRIX& previousWorld, const XMMATRIX& currentJittered,
                   const XMMATRIX& currentUnjittered, const XMMATRIX& previousVp) {
    PerDrawConstants constants{};
    XMStoreFloat4x4(&constants.current_world, world); XMStoreFloat4x4(&constants.previous_world, previousWorld);
    XMStoreFloat4x4(&constants.current_vp_jittered, currentJittered); XMStoreFloat4x4(&constants.current_vp_unjittered, currentUnjittered);
    XMStoreFloat4x4(&constants.previous_vp_unjittered, previousVp);
    constants.render_size = { static_cast<float>(g_width), static_cast<float>(g_height) };
    D3D11_MAPPED_SUBRESOURCE mapped{};
    CheckHr(g_context->Map(g_per_draw_buffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped), "Map(per-draw)");
    std::memcpy(mapped.pData, &constants, sizeof(constants)); g_context->Unmap(g_per_draw_buffer.Get(), 0);
    ID3D11Buffer* buffer = g_per_draw_buffer.Get(); g_context->VSSetConstantBuffers(0, 1, &buffer);
}

const wchar_t* ScenarioName() {
    switch (g_scenario) {
    case Scenario::static_scene: return L"static"; case Scenario::camera_translate: return L"camera-translate";
    case Scenario::camera_rotate: return L"camera-rotate"; case Scenario::rigid_object: return L"rigid-object"; default: return L"unknown";
    }
}

const wchar_t* ViewName() {
    switch (g_debug_view) { case DebugView::scene: return L"scene"; case DebugView::depth: return L"depth"; case DebugView::motion: return L"motion"; default: return L"unknown"; }
}

void UpdateTitle() {
    wchar_t title[384]{};
    swprintf_s(title, L"LTR Bridge | scenario=%s | view=%s | frame=%llu | history=%u | jitter=(%.3f, %.3f) px | 1-4 scenario, Tab debug, R reset",
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
    if (!g_previous_valid || g_pending_reset != ltr::harness::HistoryResetReason::none) { previousWorld = world; previousVp = vp; }

    const float sceneClear[4]{0.035f, 0.045f, 0.065f, 1.0f}; const float motionClear[4]{};
    g_context->ClearRenderTargetView(g_scene_color_rtv.Get(), sceneClear); g_context->ClearRenderTargetView(g_motion_rtv.Get(), motionClear);
    g_context->ClearDepthStencilView(g_depth_dsv.Get(), D3D11_CLEAR_DEPTH, 1.0f, 0);
    ID3D11RenderTargetView* sceneTargets[2]{g_scene_color_rtv.Get(), g_motion_rtv.Get()};
    g_context->OMSetRenderTargets(2, sceneTargets, g_depth_dsv.Get());
    D3D11_VIEWPORT viewport{0.0f, 0.0f, static_cast<float>(g_width), static_cast<float>(g_height), 0.0f, 1.0f}; g_context->RSSetViewports(1, &viewport);
    const UINT stride = sizeof(Vertex), offset = 0; ID3D11Buffer* vb = g_vertex_buffer.Get();
    g_context->IASetInputLayout(g_input_layout.Get()); g_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    g_context->IASetVertexBuffers(0, 1, &vb, &stride, &offset); g_context->VSSetShader(g_scene_vs.Get(), nullptr, 0); g_context->PSSetShader(g_scene_ps.Get(), nullptr, 0);
    UploadPerDraw(world, previousWorld, jitteredVp, vp, previousVp); g_context->Draw(3, 0);
    UploadPerDraw(XMMatrixIdentity(), XMMatrixIdentity(), jitteredVp, vp, previousVp); g_context->Draw(3, 3);

    g_context->OMSetRenderTargets(0, nullptr, nullptr); ID3D11RenderTargetView* backbuffer = g_backbuffer_rtv.Get(); g_context->OMSetRenderTargets(1, &backbuffer, nullptr);
    g_context->IASetInputLayout(nullptr); g_context->VSSetShader(g_debug_vs.Get(), nullptr, 0); g_context->PSSetShader(g_debug_ps.Get(), nullptr, 0);
    DebugConstants debug{}; debug.mode = static_cast<std::uint32_t>(g_debug_view);
    D3D11_MAPPED_SUBRESOURCE mapped{}; CheckHr(g_context->Map(g_debug_buffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped), "Map(debug)");
    std::memcpy(mapped.pData, &debug, sizeof(debug)); g_context->Unmap(g_debug_buffer.Get(), 0);
    ID3D11Buffer* debugBuffer = g_debug_buffer.Get(); g_context->PSSetConstantBuffers(0, 1, &debugBuffer);
    ID3D11ShaderResourceView* resources[3]{g_scene_color_srv.Get(), g_depth_srv.Get(), g_motion_srv.Get()}; g_context->PSSetShaderResources(0, 3, resources);
    ID3D11SamplerState* sampler = g_sampler.Get(); g_context->PSSetSamplers(0, 1, &sampler); g_context->Draw(3, 0);
    ID3D11ShaderResourceView* nullSrvs[3]{}; g_context->PSSetShaderResources(0, 3, nullSrvs);
    CheckHr(g_swap_chain->Present(1, 0), "Present");

    g_previous_world = world; g_previous_vp = vp; g_previous_valid = true; g_pending_reset = ltr::harness::HistoryResetReason::none;
    ++g_frame_index; UpdateTitle();
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
        else if (wParam >= '1' && wParam <= '4') { g_scenario = static_cast<Scenario>(wParam - '1'); g_pending_reset = ltr::harness::HistoryResetReason::scenario_change; }
        return 0;
    case WM_DESTROY: PostQuitMessage(0); return 0;
    default: return DefWindowProcW(window, message, wParam, lParam);
    }
}

} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int showCommand) {
    try {
        WNDCLASSEXW wc{}; wc.cbSize = sizeof(wc); wc.style = CS_HREDRAW | CS_VREDRAW; wc.lpfnWndProc = WindowProc;
        wc.hInstance = instance; wc.hCursor = LoadCursorW(nullptr, IDC_ARROW); wc.lpszClassName = kWindowClass;
        if (!RegisterClassExW(&wc)) ThrowHr("RegisterClassExW", HRESULT_FROM_WIN32(GetLastError()));
        RECT rect{0, 0, static_cast<LONG>(g_width), static_cast<LONG>(g_height)}; AdjustWindowRect(&rect, WS_OVERLAPPEDWINDOW, FALSE);
        g_window = CreateWindowExW(0, kWindowClass, kWindowTitle, WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
                                   rect.right - rect.left, rect.bottom - rect.top, nullptr, nullptr, instance, nullptr);
        if (!g_window) ThrowHr("CreateWindowExW", HRESULT_FROM_WIN32(GetLastError()));
        ShowWindow(g_window, showCommand); CreateDeviceAndPipeline();
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
