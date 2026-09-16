# Codex handoff

Updated: 2026-09-16.

## Repository state

The repository is still in research/probe phase. There is no production injector and no existing game-mod repository is a dependency.

Current local evidence is concentrated in four implemented areas:

1. **D3D11 x64 temporal harness — host-tested / visual-validated.** Controlled static, camera, rigid-object, deforming-geometry, masked-particle, blended-transparency, HUD and disocclusion scenarios exercise jitter, shader-readable depth, ground-truth MV, explicit history reset/validity, camera+depth reconstruction and an independent synthetic optical-flow baseline.
2. **XeSS 3.0.2 Native AA probe — host-tested.** D3D12 x64 executes Native AA at 1.0x with the harness Halton jitter convention, current-to-previous render-pixel MV with jitter excluded, explicit reset, responsive-mask coverage and repeated 256x144-to-4K GPU-time / temporary-heap measurements on the current RTX 4070 Ti host. This is not performance validation.
3. **D3D11 x86 -> D3D12 x64 transport — host-tested.** The probe covers 24-frame/two-generation transport, live handle replacement, separate ready/done fences, bounded depth-1/depth-2 backpressure, controlled process/device-loss paths, renderer-to-shared copy/resolve/conversion measurements through 4K, a small format matrix, two-eye isolation and independent synthetic per-eye GPU histories. CPU readback is validation-only.
4. **D3D9Ex relay — host-tested on the current machine.** A bound local D3D9Ex R10 render target is cleared, copied with `StretchRect` into a shareable R10 relay, synchronized with an event query, opened by private x86 D3D11, converted into the existing shared RGBA8 transport and consumed by the x64 D3D12 host. Both `64x64 -> 96x72` and `1920x1080 -> 2560x1440` profiles survive one `ResetEx`/resource transition with zero mismatches across repeated runs. Classic D3D9 shared creation remains a current-host negative boundary.

The separate OpenXR x64 bootstrap is **host-tested** for loader/instance/runtime negotiation. SteamVR exposes both D3D11 and D3D12 enable extensions, but the current runs returned `XR_ERROR_FORM_FACTOR_UNAVAILABLE`; no HMD system, graphics session, swapchain, frame loop, presentation or physical-headset claim exists yet.

## Evidence boundaries

- All local transport/backend numbers are short, synthetic, single-host measurements unless a document explicitly says otherwise.
- D3D9Ex success, classic-D3D9 failure and BGRA8 behavior are host/driver scoped.
- The bridge has not yet been exercised through a real intercepted game renderer.
- The current stereo transport loop is synthetic and does not establish concurrent full-resolution eye processing, OpenXR pacing or headset latency.
- XeSS Native AA is the only real reconstruction backend executed locally so far; DLSS/DLAA, FidelityFX and XeSS SR comparison remains pending.
- Motion-vector coverage for real skinned/cloth geometry, particles, transparency and first-person/VR objects remains unresolved.

## Next concrete work

1. Move the proven D3D9Ex relay into a controlled real D3D9Ex scene/game interception and test engine-owned render-target hazards, scheduling, reset behavior and depth/transform visibility.
2. Compare that native D3D9Ex route with dgVoodoo2 on the same controlled target before selecting a preferred D3D9 architecture.
3. Reproduce the D3D10 -> private D3D11 relay path locally.
4. Continue backend comparison from the existing XeSS Native AA result: add DLSS/DLAA and FidelityFX mappings, then XeSS SR, while keeping backend-specific optional inputs explicit.
5. When an HMD is visible to the OpenXR runtime, rerun `tools/run_openxr_runtime_probe.ps1` to collect stereo-view and D3D11/D3D12 adapter requirements before adding graphics session, swapchains and pacing.
6. Extend renderer-transfer testing only where it answers a new uncertainty: real engine hazards/scaling, simultaneous attachments, concurrent full-resolution eyes, or a genuinely different device/host failure timing point.
7. For real SR, validate renderer-resolution side effects such as post-processing, HUD/highlights, particles/billboards, mip bias and secondary-camera/pass assumptions.

## Reference snapshots

- BioShock VR DLSS/DLAA: `v0.2.17-en` / `8671fc87c4646140419ea64bd6e60d59fcac4723`.
- Rogue Trader EnhancedGraphics: `v2.2` / `01b1cd816db08f2b1c6c68b1319c6f44dd61bdd6`.
- OFXR Bridge case study: `dad56acafc6e1dde219940427738b926cf2ea555`.
- XeSS SDK: `v3.0.2` / `8fe81bd`.
- AMD FSR SDK: `2.3.0`; inspected signed DX12 binaries are x64.

## Working constraints

Keep source-API adaptation, temporal-data production, transport and reconstruction backend separable until experiments justify coupling them. Preserve per-eye resource/history identity as an architectural requirement. Do not begin a production injector or freeze a universal public temporal ABI during the current research phase.
