# Roadmap

The roadmap is evidence-driven. Items must be rechecked against repository state before work begins.

## Phase 0 — research baseline

Status: **implemented as the research baseline; later phases now provide host-tested evidence for the temporal harness, x86/x64 transport, XeSS Native AA, OpenXR runtime bootstrap and the current-host D3D9Ex relay**.

- [x] Establish independent repository scope and working rules.
- [x] Record current DLSS5-Feeder architecture, including x86 helper and D3D10 relay findings.
- [x] Record current OptiScaler role and limits for legacy games.
- [x] Record ReShade API/depth capabilities relevant to D3D9/10/11 prototyping.
- [x] Compare published DLSS, FidelityFX temporal upscaling, and XeSS temporal inputs.
- [x] Define provisional architecture and temporal contract.
- [x] Define initial D3D9, D3D10, x86/x64, and VR questions.
- [x] Verify current Streamline/DLSS, FidelityFX/FSR and XeSS execution constraints relevant to host API and process bitness.
- [x] Review the current NVIDIA RTX SDK/NGX distribution license from its primary license text.
- [x] Identify the documented D3D9 -> D3D11 shared-texture path and the D3D9-vs-D3D9Ex ambiguity that requires a probe.
- [x] Record current D3D8 routes: native interception, `d3d8to9` -> D3D9 and dgVoodoo2 -> D3D11/12.
- [x] Record concrete depth-access behavior from ReShade across D3D9/10/11 and current optical-flow/MV-provider limitations.
- [x] Separate real renderer projection jitter from post-process/synthetic sampling jitter.
- [x] Record complementary case studies: BioShock for x86/x64 stereo transport and strict temporal identity; Rogue Trader for renderer-native internal resolution, jitter and camera/object MV integration; OFXR Bridge for OpenXR synthetic-frame presentation, resource lifetime and pacing.
- [ ] Pin additional upstream source commits where conclusions currently rely on moving `main` branches.
- [x] Review current primary distribution/license texts for AMD FidelityFX signed SDK binaries, Intel XeSS SDK, and ReShade/API headers.
- [ ] Re-check the exact backend binaries, source files and third-party notices selected for packaging immediately before any release.

## Phase 1 — D3D11 x64 temporal harness

Status: **implemented, host-tested and visual-validated for static/camera/rigid-object/deforming-geometry/masked-particle/blended-transparency/HUD/disocclusion controlled cases, explicit history validity and the controlled camera+depth baseline; XeSS Native AA backend host-tests now use coherent harness jitter/MV semantics and include repeated GPU-time/temporary-heap measurements from 256x144 through 4K; expanded-content, visual, multi-host and sustained performance validation pending**.

Goal: prove temporal correctness without legacy transport complexity.

Success criteria:

- controlled color, depth, motion vectors, and projection jitter;
- compare renderer-native/ground-truth motion against camera+depth reconstruction and an image-space optical-flow baseline; preserve coverage/exclusion and confidence/validity diagnostics;
- explicit history reset;
- 1:1 native-resolution temporal AA path first;
- validate jitter semantics at native AA and at multiple sub-native SR ratios;
- debug views that validate depth/MV direction and scale;
- visual validation on static camera, translation, rotation, animated geometry, cloth/skinned geometry, particles, HUD/highlights, resize, and history reset;
- frame-time and VRAM measurements.

Decision gate: only proceed to legacy integration after the contract can be validated independently of a game.

Current foundation: the standalone x64 D3D11 harness now provides 1:1 scene color, shader-readable hardware depth, renderer projection jitter, current-pixel -> previous-pixel ground-truth camera/rigid-object motion, explicit history resets, provisional per-view identity, and scene/depth/MV diagnostics. Deterministic GPU readback is wired into CTest and Windows CI, and the controlled diagnostics have been visually inspected. A camera+depth MV baseline is compared deterministically against renderer ground truth: it matches controlled camera translation/rotation within sub-0.01 px maximum error and explicitly fails non-camera motion, as expected for a camera-only provider. The harness also has procedural deforming-geometry and masked-particle proxies plus an alpha-blended moving layer. The transparency probe demonstrates that one scene-color pixel can contain foreground/background contributions while a single MV/surface slot represents only one contributor. A separate screen-space HUD probe demonstrates the complementary failure mode: 11,886 overlay pixels change color history while depth/MV/surface identity remain static and therefore classify all 11,886 as reusable history. A surface-identity history-validity oracle distinguishes correct geometry motion from reusable geometry history and includes a dedicated disocclusion case with deterministic rejection criteria. A separate CPU/synthetic optical-flow baseline measures image-space motion and confidence against known truth across static, camera, rigid-object and disocclusion cases without renderer depth or matrices; it is integrated into root CTest/CI and remains synthetic evidence only. An optional D3D12 x64 XeSS 3.0.2 probe now executes real Native AA at 1.0x with the same 8-sample Halton jitter sequence and motion semantics as the harness: high-resolution current-to-previous render-pixel MV, jitter excluded and dilated. On the local RTX 4070 Ti host its controlled moving-HUD region measured `8.3259` baseline MAE versus `5.6902` with the responsive mask. Repeated D3D12 timestamp measurements across five runs per resolution measured baseline means of `0.1950 ms` at 256x144, `0.4430 ms` at 720p, `0.6676 ms` at 1080p, `0.9647 ms` at 1440p and `2.0014 ms` at 4K. `xessGetProperties` temporary-heap capacity scaled from `1.8125 MiB` to `249.5625 MiB` per context over the same range. These are **host-tested** single-host measurements, not `performance-validated` support claims. Real-content cases, additional reconstruction backends, real-content optical-flow validation, broader visual/performance validation, SR, and stereo remain pending.

## Phase 2 — D3D11 x86 -> x64 bridge probe

Status: **multiframe transport, true mid-run resource replacement, controlled process/device-loss handling including host loss during depth-1 slot reuse, renderer-to-shared transfer accounting through 4K, 4x-MSAA resolve and R10-to-RGBA8 conversion, a small format/interoperability matrix, bounded depth-1/depth-2 backpressure, controlled two-eye transport and synthetic independent per-eye GPU-history semantics implemented and host-tested; real-renderer hazards/scaling, broader format/sample-count combinations, mid-dispatch failure timing and OpenXR/headset execution pending**.

Goal: validate GPU-resident cross-bitness transport independently of a reconstruction SDK.

Probe shape:

- x86 D3D11 producer;
- shared texture set;
- shared fence/synchronization;
- x64 D3D12 consumer;
- deterministic GPU modification;
- result returned to x86;
- no CPU pixel round-trip.

Measure copies, stalls, queue waits, resize, process failure, adapter identity, and cleanup.

Current foundation: the x64 D3D12 host launches the x86 D3D11 client with only generation 0 (`64x64`). The client runs 12 deterministic frames, then the host waits for frame 12 on the `done` fence, retires its generation-0 resource, creates generation 1 (`96x72`), duplicates that NT handle into the live x86 process and sends the handle plus generation/extent/protocol metadata over a small anonymous-pipe control record. The x86 process retires generation 0 after its final validation, validates and opens the new handle, then both sides complete another 12 frames with the same monotonic fence timeline. All 24 transformed frames validate with zero mismatches. The multiframe path uses separate unidirectional shared fences: D3D11 signals `ready`, D3D12 signals `done`. The x64 transform is in-place (`0` transport GPU copies); one x86 GPU copy per frame exists only for staging/readback validation. Five additional dynamic-replacement runs passed; per-run mean D3D12 compute time averaged `4.0874 us` (`3.925–4.352 us` between run means) and per-run mean validation signal-to-readback wall time averaged `1.9595 ms` (`1.6301–2.1445 ms` between run means). These remain short single-host/synthetic measurements rather than performance validation. Protocol mismatch, adapter mismatch, resource-contract mismatch, a `250 ms` host-stall timeout and an invalid dynamic generation marker are deterministic negative tests. The format matrix adds three deliberately non-contract generation-0 resources: `R16G16B16A16_FLOAT` and `R8G8B8A8_UINT` open across D3D12 -> D3D11 and are rejected by the x86 descriptor contract, while `R32G32B32A32_FLOAT` reaches an earlier host-specific boundary where D3D11 `OpenSharedResource1` returns `E_INVALIDARG`. Abrupt client termination, controlled D3D12 device removal (`DXGI_ERROR_DEVICE_REMOVED`) and abrupt host termination are also host-tested. The x86 detects host loss through an inherited process handle; the experiment observed that the fence event may wake first during teardown, so host liveness is checked independently with a bounded `250 ms` disambiguation. A separate `64x64` ring probe validates bounded pressure with pre-created depth-1 and depth-2 resource sets: five runs per depth completed `24` frames with zero mismatches and `max_in_flight` exactly equal to ring depth. A one-time `50 ms` host delay forces real reuse waits; depth 1 reported `23/23` pending reuse checks on every run and depth 2 reported `16–21/22`. A new negative terminates the x64 host after the producer signals depth-1 frame 0 but before the corresponding `done` value is valid; the x86 is then at the first slot-reuse wait. The complete driver and five extra repetitions all detected the host process and exited without entering the normal GPU wait/readback path. In all five repetitions the fence event woke first, so the same bounded process-liveness disambiguation remained necessary. This proves one in-flight reuse failure point, not arbitrary command-list/device-loss timing. The controlled stereo mode uses two independent `64x64` eye resources plus separate D3D11-ready/D3D12-done fences, runs 12 temporal frames per eye, and validates distinct per-eye RGB payloads and alpha identity markers. Five additional stereo runs completed with zero mismatches and zero cross-eye contamination; a deliberate right-eye marker injection into the left stream was detected on all `49,152` affected pixels. A separate stereo-history mode keeps one host-side GPU UAV history per eye: frame 0 explicitly initializes each eye history from that eye's transformed payload, while the next 11 frames must keep returning the same eye's prior history despite changing source input. The full driver and five additional history runs completed with zero mismatches and zero cross-eye contamination. Deliberately swapping the history UAVs after frame 0 contaminated all `90,112` subsequent view pixels (`64 * 64 * 11 * 2`) and was rejected. This establishes synthetic per-eye history/reset isolation only. Real-renderer hazards/scaling, broader format/sample-count combinations, mid-dispatch failure timing, OpenXR presentation and physical-headset execution remain pending.

A renderer-transfer matrix now keeps the same RGBA8 cross-process contract while varying the x86-side source path. The original tiny `CopyResource` case remains host-tested. A high-resolution same-format copy covers `1920x1080 -> 3840x2160`; five repeated runs averaged `23.9184 us` at 1080p and `93.9013 us` at 4K. A 4x-MSAA `R8G8B8A8_UNORM` source resolves into the shared single-sample resource at `1920x1080 -> 2560x1440`; five runs averaged `15.0016 us` and `21.1627 us` for the resolve itself. A local `R10G10B10A2_UNORM` source is converted into shared `R8G8B8A8_UNORM` by a fullscreen D3D11 shader at the same two extents; five runs averaged `29.1685 us` and `46.3008 us`. Every repeated run completed 24 frames, one live generation replacement and zero mismatches. These are host-tested synthetic transfer intervals, not performance-validated engine costs.

## Phase 3 — real Call of Juarez / classic-D3D9 vertical slice

Status: **real-game observation plus a one-shot D3D9-facing -> D3D9Ex -> D3D11 x86 -> D3D12 x64 color path are host-tested; the exact two-slot transport is host-tested offline and its shared client is connected to the observer behind an opt-in, compile-tested switch; live multiframe and reset/resource-generation validation remain pending**.

Use the clean Steam Call of Juarez renderer to establish the smallest complete path before expanding the matrix:

- [x] establish that the game uses classic `IDirect3D9`, not D3D9Ex, and reaches the real renderer through `Direct3DCreate9`;
- [x] observe the real `Present` boundary with a `2560x1440` `D3DFMT_A8R8G8B8` render target, `D3DFMT_D24X8` depth, transforms and one swapchain visible;
- [x] prove that the simple shared-resource relay is unavailable on this host both for classic-D3D9 shared creation and for opening D3D9Ex-created shared handles from a classic device;
- [x] prove synthetically that an `IDirect3D9Ex` root exposed through `IDirect3D9::CreateDevice` yields an Ex-capable device with the same R10/RGBA16F/BGRA8 sharing behavior;
- [x] promote Call of Juarez's intercepted `Direct3DCreate9` to `Direct3DCreate9Ex` while preserving the base interface and verify that the real game still reaches `Present` with an Ex-capable device;
- [x] copy the real `2560x1440` A8R8G8B8 frame into a shared same-format relay, complete with a D3D9 event query, open it in D3D11, and compare five source/consumer samples with zero mismatches;
- [x] copy that real D3D11 frame into an NT-shared BGRA8 texture and open/validate it from a separate x64 D3D12 process; five samples match with zero mismatches;
- [x] isolate the failed persistent handoff from the game and host-test the replacement ownership contract offline: x64 D3D12 owns two shared BGRA8 slots plus the `done` fence, duplicates their handles into x86, x86 D3D11 opens the slots and owns the named `ready` fence; 5 x 12-frame `64x64` runs and 3 x 12-frame `2560x1440` runs complete with zero content mismatches under forced two-slot backpressure;
- [x] implement the opt-in replacement for the one-shot handoff using the same host-tested bounded ready/done client, without validation readback in the game-side frame path; the observer join is compile-tested and still requires live validation;
- [ ] live-test 12 real submissions/completions and then a reset-triggered generation replacement with clean fail-open teardown. The prepared runner now makes the x64 sink execute a real D3D12 consumer copy before every `done`, verifies color/depth/transforms at the real `Present` boundary, and can use `-RequireResetGeneration` to stall generation 1 deliberately so a user-triggered game `Reset` exercises active-child teardown before requiring a fully completed post-reset generation;
- [ ] extend observation only where required to identify scene color/depth, transforms, draws/passes, HUD composition and reset lifecycle for temporal semantics;
- [ ] replace probe-style global/static hook ownership with per-device/per-swapchain state and explicit resource generations where the live lifecycle test shows it is required;
- [ ] emit the real renderer inputs through the internal `TemporalFrame` semantics already exercised by the harness, including frame/view identity, history validity and reset reason;
- [x] connect the real observer to the existing x86 -> x64 GPU transport in source using the shared two-slot client; the exact contract is host-tested offline and the observer integration is compile-tested;
- [ ] execute XeSS Native AA at 1:1 as the first real reconstruction backend;
- [ ] return or compose the reconstructed result without making reconstruction failure fatal to the renderer;
- [x] keep validation readback out of the multiframe game-side hot path; CPU maps/readbacks remain confined to probes and consumer-side validation.

Decision gate: do not add another source API or backend until one real frame can be traced end to end with correct reset/history/resource identity and useful timing data.

Optimization work in this phase is measurement-driven. Record event-query stalls, GPU copies, ring/backpressure behavior, allocations, logging cost, frame pacing and latency in the real path; change the synthetic bridge only when the real renderer exposes a new hazard.

**Observed failed real-game multiframe attempt, offline resolution and new integration (2026-09-17):** the initial two-slot/12-frame game experiment created D3D11-owned `SHARED_NTHANDLE | SHARED_KEYEDMUTEX` transport textures and launched a persistent x64 sink, but ended at `stage=completion RESULT FAIL`. A game-free probe reproduced the failure: fences reached 12 while D3D12 content validation read zero. The replacement direction makes x64 D3D12 own the slots and `done`, while x86 D3D11 opens them and owns `ready`. Its client is now shared between the synthetic producer and observer. After extraction, five `64x64` and three `2560x1440` 12-frame runs completed with zero mismatches and forced backpressure. The observer integration now keeps one D3D9/D3D11 source relay per transport slot. Since slot reuse is admitted only after that slot's prior `done` value, and `done` is downstream of the matching `ready` signal and D3D11 copy, the redundant per-frame D3D11 event-query wait has been removed while the D3D9 event query remains the producer-to-D3D11 handoff. The synthetic producer now also waits for slot admission before rewriting its matching source; 3/3 `64x64` and 2/2 `2560x1440` forced-backpressure runs passed 12/12 with zero mismatches after this refinement. The observer compiles, skips work under slot pressure, recreates state lazily after reset and fails open. This is **implemented and compile-tested**, while real-game multiframe and reset-generation behavior remain **experiment-pending**.

## Phase 4 — same-game native vs dgVoodoo temporal provenance

Status: **controlled comparison completed; real-game comparison pending**.

Run the same Call of Juarez build through dgVoodoo2 after the lighter D3D9Ex compatibility route has been measured for lifecycle, compatibility and temporal provenance, or earlier if a real game state exposes a blocker. The official D3D12 addon boundary is already host-tested for translated presentation resources; determine whether frontend/observer routes retain useful original depth, transforms, pass identity or another basis for temporal data. Do not infer those inputs from presentation callbacks.

Use the same game, scene and measurements so compatibility, temporal-data visibility, copies, scheduling, resets and frame-time effects are comparable.

## Phase 5 — backend comparison and SR

Status: **started: XeSS 3.0.2 Native AA on D3D12 x64 is implemented and host-tested at 1.0x; broader comparison is deliberately deferred until the real vertical slice works**.

Once the vertical contract is exercised by a real renderer, map it to DLAA/DLSS-compatible integration, FidelityFX temporal upscaling and XeSS SR where the available SDK/API path permits it. Document backend-specific inputs instead of hiding them behind a premature lowest-common-denominator interface.

For real SR, validate insertion point and renderer-resolution side effects separately from backend correctness: post-processing resolution, screen-space particles/billboards, mip bias and secondary-camera/pass assumptions must not silently inherit display-resolution semantics.

## Phase 6 — D3D10 expansion

Status: **legacy shared-resource relay, event-query synchronization and end-to-end x86 -> x64 transport implemented and host-tested on the current RTX 4070 Ti; keyed-mutex creation unavailable on this host; depth/MV/SM4 temporal-data and real-game integration pending**.

The transport route is already proved synthetically. Extend it only after the D3D9 vertical slice establishes the reusable temporal/lifecycle contract: preserve real depth, add an SM4-compatible temporal-data provider, place it behind a real interception boundary, and repeat capability testing on additional hardware/drivers.

Also test Shader Model 4-compatible MV reconstruction paths and compare event-query synchronization with any keyed-mutex support actually observed on test hardware.

Current-host result: a Win32 D3D10.1 device at feature level 10.0 renders deterministic R10 color, copies it into a `D3D10_RESOURCE_MISC_SHARED` R10 relay and waits for `D3D10_QUERY_EVENT`. A private same-adapter D3D11 device opens the relay and validates it across `640x360 -> 1280x720` recreation. Five standalone repetitions passed with zero mismatches. The same source route is integrated into the existing x86 -> x64 bridge through the D3D11 R10-to-RGBA8 fullscreen conversion; five 24-frame repetitions passed with one generation transition and zero mismatches. Across those bridge runs, the D3D10 copy+event CPU-wall mean averaged about `0.1866 ms` between run means and the D3D11 conversion about `5.0531 us`. These are short synthetic single-host measurements, not performance validation. A keyed-mutex capability probe returns `E_INVALIDARG` (`0x80070057`) during D3D10 resource creation on this host, so that path is recorded as unavailable rather than generalized as unsupported.

## Completed research block — D3D9 controlled architecture comparison

Status: **controlled-target comparison host-tested on the current machine: native D3D9Ex interception/relay works, classic D3D9 shared creation and D3D9Ex-handle opening from classic D3D9 are negative boundaries, dgVoodoo2 2.87.5 rejects the native D3D9 shared-relay design as-is, and the official dgVoodoo D3D12 addon API exposes a working translated presentation boundary**.

Start with a minimal API-interop probe before attempting reconstruction:

- [x] classic D3D9 producer -> shared-texture creation attempt -> D3D11 consumer boundary;
- [x] D3D9Ex producer -> shared texture -> D3D11 consumer;
- [x] D3D9Ex producer -> shared handle -> classic D3D9 open attempt for R10, RGBA16F and BGRA8-control resources;
- [x] verify first-host format behavior, pixel contents, explicit event-query synchronization and resource recreation;
- [x] copy from a local D3D9Ex render target into the relay texture with `StretchRect` and account for completion cost;
- [x] exercise D3D9Ex `ResetEx` and relay-resource recreation;
- [x] feed the D3D11 relay into the existing x86 -> x64 D3D12 transport path;
- [x] repeat the integrated route with a bound D3D9Ex render target at `1920x1080 -> 2560x1440` and preserve reset/recreation plus deterministic validation.
- [x] exercise a controlled D3D9Ex scene with engine-owned color/depth/vertex resources, fixed-function transforms, depth-tested draws and capture while color/depth remain bound; carry the same scene through the x86 -> x64 bridge.
- [x] place the controlled scene in a separate executable and capture it from a separately built interceptor DLL by observing `Direct3DCreate9Ex`, `CreateDeviceEx`, `EndScene` and `ResetEx`; verify state preservation and resource recreation without target-side capture calls.
- [x] run the same target through official dgVoodoo2 2.87.5 and record the native R10/BGRA8 shared-relay compatibility boundary.
- [x] build a minimal addon against the external dgVoodoo API 2.87.5 package and observe the D3D12 root, adapter/device, swapchain and presentation lifecycle without modifying resources.
- [x] repeat the dgVoodoo D3D12-addon path five times across `640x360 -> 1280x720` reset/recreation and require 12/12 non-null source/destination presentation resources.

Current-host result: classic D3D9 returns `D3DERR_INVALIDCALL` for every tested `CreateTexture(..., pSharedHandle)` case. D3D9Ex successfully shares the documented R10/RGBA16F relay formats; documented RGBA8 fails here and BGRA8 remains a driver-specific control. A cross-runtime matrix then creates R10, RGBA16F and BGRA8-control shared resources in D3D9Ex and asks a classic D3D9 device to open the same handles; all three opens also return `D3DERR_INVALIDCALL`. The simpler D3D9Ex relay path remains stable through 1440p and `ResetEx`. The controlled-scene probe renders overlapping red/green geometry through an engine-owned vertex buffer into an engine-owned R10 target with D24S8 depth and explicit fixed-function world/view/projection state. Five standalone `640x360 -> 1280x720` runs pass with zero mismatches, and the same scene reaches the x86 -> x64 bridge in five 24-frame zero-mismatch runs. A separate target/interceptor pair validates the external native boundary: the target owns the scene and renderer loop, while an external DLL hooks API/COM entry points, observes the still-bound color/depth/world state after `EndScene`, copies to its own shared R10 relay, validates through private D3D11, and survives one intercepted `ResetEx`. Five repeated runs pass with zero mismatches and state preservation; `StretchRect + event` run means are about `0.1903–0.2041 ms` (about `0.1990 ms` average). Under dgVoodoo2 2.87.5, the same R10 target fails at D3D9 target creation and the BGRA8 profile reaches rendering but fails when the interceptor asks dgVoodoo for the shared D3D9Ex relay, so that native relay design is not reusable through the wrapper as-is. With `OutputAPI=d3d12_fl11_0`, the official addon API instead exposes a working modern presentation boundary: five runs report API version `0x287`, a non-null D3D12 device, two swapchain generations, 12 matched present begin/end callbacks and non-null source/destination resources on every frame. The callback source is `DXGI_FORMAT_R8G8B8A8_TYPELESS` and the drawing target is `DXGI_FORMAT_R8G8B8A8_UNORM` in this profile. These are controlled single-host results; the Call of Juarez observation in Phase 3 is the separate real-game evidence.

The controlled target has now compared the relevant route boundaries:

1. native D3D9 interception plus a dedicated transport;
2. native D3D9/D3D9Ex interception -> constrained shared-texture D3D11 relay -> modern path, when supported;
3. dgVoodoo2 translation -> modern backend observation; the D3D12 addon presentation boundary is host-tested while downstream D3D11-style integration remains a separate experiment.

The unresolved comparison has moved to Phase 4 because real-game temporal provenance, not another controlled presentation test, is now the deciding evidence.

## D3D8 comparison — after the D3D9 boundary is understood

Status: **planned**.

Use the same controlled D3D8 target to compare native interception, `d3d8to9` followed by the proven D3D9 route, and dgVoodoo2 followed by the modern route. The decision should include renderer-state visibility, depth/MV opportunity, compatibility, proxy coexistence, redistribution and performance.

## Phase 7 — controlled stereo/OpenXR harness

Status: **runtime/bootstrap probe implemented and host-tested; graphics session, frame loop, pacing, swapchain lifecycle and physical-headset validation remain planned**.

Two independent eye targets, histories, matrices, depth and MVs. Measure temporal divergence, latency, and submission order before trying a real VR mod.

Current bootstrap evidence: a standalone x64 probe using external Khronos OpenXR SDK `1.1.63` headers (`f2448a8797c85814aa892efc1ab8707900fbcc78`) dynamically loads the active SteamVR OpenXR loader. On the current host SteamVR/OpenXR runtime `2.17.10` rejects an OpenXR `1.1.63` application request with `XR_ERROR_API_VERSION_UNSUPPORTED` and succeeds when the probe retries at OpenXR `1.0.0`. It exposes both `XR_KHR_D3D11_enable` and `XR_KHR_D3D12_enable`. The current machine state returned `XR_ERROR_FORM_FACTOR_UNAVAILABLE` for `XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY` on repeated runs, so no system ID, per-view recommended extents, graphics-adapter requirements, session, `xrWaitFrame`, swapchain or headset result has been promoted from this probe. The next OpenXR experiment starts only once an HMD system is available to the runtime.

## Phase 8 — VR mod validation

Status: **planned**.

Integrate only as a consumer/case study. Require `vr-headset-validated` evidence before claiming VR support.

## Promotion policy

No row in `docs/COMPATIBILITY.md` becomes `supported` from compilation, a synthetic transport test, or one screenshot. Promotion requires the relevant runtime, visual, performance, and when applicable headset validation states.
