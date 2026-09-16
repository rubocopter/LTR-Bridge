# OpenXR runtime/bootstrap probe

Status: **implemented and host-tested for loader/instance/runtime negotiation; HMD system, view configuration, graphics requirements, session, pacing, presentation and headset execution remain experiment-pending**.

## Purpose

Before adding an OpenXR presentation or pacing layer, the project needs a small experiment that answers a narrower question: can the current x64 host negotiate with the active OpenXR runtime, and when an HMD is available can it obtain the runtime's stereo-view and Direct3D adapter requirements without coupling that logic to the cross-bitness bridge?

The probe lives in `src/openxr_runtime_probe/` and is built only when `LTR_ENABLE_OPENXR_RUNTIME_PROBE=ON`. `tools/run_openxr_runtime_probe.ps1` locates the active runtime's loader where possible, builds an x64 executable and runs the probe. Khronos headers are supplied from an external checkout through `LTR_OPENXR_SDK_ROOT`; no OpenXR SDK or OFXR Bridge code is vendored into this repository.

## Pinned development input

The first host run used Khronos `OpenXR-SDK` `release-1.1.63`, commit `f2448a8797c85814aa892efc1ab8707900fbcc78` (2026-09-01). The probe dynamically loads `xrGetInstanceProcAddr` from the installed loader and resolves the API entry points through that dispatch path.

## Current host result — 2026-09-16

**Host-tested:** two consecutive executions loaded the active SteamVR x64 OpenXR loader and completed the runtime-bootstrap scope with `RESULT PASS`.

**Observed:** SteamVR reported runtime name `SteamVR/OpenXR`, runtime version `2.17.10`, `41` instance extensions, `XR_KHR_D3D11_enable=1`, and `XR_KHR_D3D12_enable=1`. Creating an instance with header/current API version `1.1.63` returned `XR_ERROR_API_VERSION_UNSUPPORTED` (`-4`); the probe's explicit fallback to OpenXR `1.0.0` succeeded.

**Observed current-machine limitation:** `xrGetSystem` for `XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY` returned `XR_ERROR_FORM_FACTOR_UNAVAILABLE` (`-35`) on both runs. Because no HMD system ID was available, the probe did not claim results for primary-stereo view count/extents or for the D3D11/D3D12 graphics-requirement LUIDs.

## Next success gate

With an HMD visible to the runtime, the existing probe should first complete its already implemented system path:

- enumerate `XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO` and record per-view recommended extents/sample counts;
- query `xrGetD3D11GraphicsRequirementsKHR` and `xrGetD3D12GraphicsRequirementsKHR` when those extensions are exposed;
- resolve both reported LUIDs through DXGI and verify whether the runtime requires the same adapter for both graphics APIs.

Only after that gate passes should a separate controlled probe add a real graphics session, swapchains, view location and the `xrWaitFrame`/`xrBeginFrame`/`xrEndFrame` lifecycle. Accepted API calls alone will still not constitute physical-headset or presentation-quality validation.

## Evidence boundary

This probe currently establishes loader/instance/runtime negotiation on one Windows host. It does not establish OpenXR presentation, pacing, synthetic-frame insertion, swapchain recreation, recentering, pose/FOV correctness, frame generation, physical-headset scanout, compatibility with a game, or performance.
