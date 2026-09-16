# Codex handoff

Updated: 2026-09-16.

## Repository state

The repository is still in research/probe phase. There is no production injector and no existing game-mod repository is a dependency.

The documentation has been reconciled to the current probe state: completed transport/D3D9Ex/dgVoodoo2/D3D10/XeSS/OpenXR-bootstrap work is no longer listed as merely planned. D3D9 now includes both a controlled native external-process interception boundary and a controlled dgVoodoo2 2.87.5 D3D12-addon presentation boundary. Real-game integration, D3D10 temporal-data integration, broader reconstruction-backend comparison, OpenXR presentation and headset validation remain explicit pending work.

Current local evidence is concentrated in six implemented areas:

1. **D3D11 x64 temporal harness — host-tested / visual-validated.** Controlled static, camera, rigid-object, deforming-geometry, masked-particle, blended-transparency, HUD and disocclusion scenarios exercise jitter, shader-readable depth, ground-truth MV, explicit history reset/validity, camera+depth reconstruction and an independent synthetic optical-flow baseline.
2. **XeSS 3.0.2 Native AA probe — host-tested.** D3D12 x64 executes Native AA at 1.0x with the harness Halton jitter convention, current-to-previous render-pixel MV with jitter excluded, explicit reset, responsive-mask coverage and repeated 256x144-to-4K GPU-time / temporary-heap measurements on the current RTX 4070 Ti host. This is not performance validation.
3. **D3D11 x86 -> D3D12 x64 transport — host-tested.** The probe covers 24-frame/two-generation transport, live handle replacement, separate ready/done fences, bounded depth-1/depth-2 backpressure, controlled process/device-loss paths, renderer-to-shared copy/resolve/conversion measurements through 4K, a small format matrix, two-eye isolation and independent synthetic per-eye GPU histories. CPU readback is validation-only.
4. **D3D9Ex relay, controlled scene and external interception boundary — host-tested on the current machine.** The relay route remains stable through 1440p. `d3d9ex_scene_probe` covers engine-owned R10 color, D24S8 depth, vertex resources, transforms and overlapping depth-tested draws, and the scene also reaches the x86 -> x64 bridge. `d3d9ex_intercept_probe` now runs the renderer in a separate executable and capture in a separate DLL that hooks `Direct3DCreate9Ex`, `CreateDeviceEx`, `EndScene` and `ResetEx`. Five repeated 12-frame runs preserve the bound color/depth/world state, survive `640x360 -> 1280x720` recreation and finish with zero mismatches; `StretchRect + event` run means average about `0.1990 ms`. The target cooperatively loads the DLL, so generic injection and a real-game renderer remain pending. Classic D3D9 shared creation remains a current-host negative boundary.
5. **dgVoodoo2 2.87.5 comparison and D3D12 addon boundary — host-tested on the current machine.** The native D3D9Ex shared-relay architecture does not traverse dgVoodoo unchanged: R10 target creation is rejected and BGRA8 reaches rendering but shared-relay creation is rejected with `D3DERR_INVALIDCALL`. A separate addon probe built against the external official API package succeeds with the D3D12 backend. Five repeated `d3d12_fl11_0` runs observe API version `0x287`, a non-null D3D12 device, two `640x360 -> 1280x720` swapchain generations and 12/12 matched presentation callbacks with non-null source/destination resources. The callback exposes translated presentation textures; original D3D9 depth/world/MV visibility is not established there.
6. **D3D10.1 -> private D3D11 relay — host-tested on the current machine.** The x86 probe uses a D3D10.1 device at feature level 10.0, copies local R10 color into a legacy shared R10 texture, waits on a D3D10 event query and opens that resource from a same-adapter private D3D11 device. Five standalone `640x360 -> 1280x720` runs pass with zero mismatches. The integrated x86 -> x64 route reuses the existing D3D11 R10-to-RGBA8 conversion and D3D12 host; five 24-frame runs pass with zero mismatches. Bridge-run D3D10 copy+event means were `0.1832–0.1902 ms` and D3D11 conversion means `4.9053–5.2747 us`. Keyed-mutex resource creation returns `E_INVALIDARG` on this RTX 4070 Ti, so event-query synchronization is the proven current-host route. Depth/MV/SM4 temporal integration remains pending.

The separate OpenXR x64 bootstrap is **host-tested** for loader/instance/runtime negotiation. SteamVR exposes both D3D11 and D3D12 enable extensions, but the current runs returned `XR_ERROR_FORM_FACTOR_UNAVAILABLE`; no HMD system, graphics session, swapchain, frame loop, presentation or physical-headset claim exists yet.

## Evidence boundaries

- All local transport/backend numbers are short, synthetic, single-host measurements unless a document explicitly says otherwise.
- D3D9Ex success, classic-D3D9 failure and BGRA8 behavior are host/driver scoped.
- dgVoodoo results are scoped to official package/API `2.87.5` on the current host. The proven addon boundary is D3D12 presentation; it does not establish original D3D9 depth, transform or MV visibility.
- D3D10 event-query success and keyed-mutex creation failure are also host/driver scoped; no universal D3D10 synchronization claim follows from this machine.
- D3D9Ex capture has now been exercised behind a separate target/interceptor boundary, but the target cooperatively loads the interceptor and remains synthetic; arbitrary-process injection and real-game integration are not established.
- The current stereo transport loop is synthetic and does not establish concurrent full-resolution eye processing, OpenXR pacing or headset latency.
- XeSS Native AA is the only real reconstruction backend executed locally so far; DLSS/DLAA, FidelityFX and XeSS SR comparison remains pending.
- Motion-vector coverage for real skinned/cloth geometry, particles, transparency and first-person/VR objects remains unresolved.

## Next concrete work

1. Take both host-tested D3D9 boundaries to a real D3D9Ex title: native `EndScene` interception for original renderer state, and dgVoodoo's D3D12 addon for translated presentation resources. Record game-specific depth/transform/MV visibility before making an architecture decision.
2. Investigate dgVoodoo's frontend D3D observer interfaces only as a focused experiment for pre-presentation resource/state provenance; do not infer those inputs from the successful D3D12 presentation callback.
3. Extend the host-tested D3D10 relay into depth preservation and an SM4-compatible temporal-data provider, then place it behind a real interception boundary and repeat the capability probe on additional hardware/drivers.
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
