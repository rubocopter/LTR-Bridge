# Codex handoff

Updated: 2026-09-14.

## Repository state

This workspace started empty. The project now includes the first controlled D3D11 x64 Phase 1 research harness; there is still no production injector or reconstruction runtime.

Core files now present:

- `AGENTS.md`
- `README.md`
- `ARCHITECTURE.md`
- `ROADMAP.md`
- `docs/RESEARCH.md`
- `docs/COMPATIBILITY.md`
- `docs/TEMPORAL_CONTRACT.md`
- `docs/X86_X64_BRIDGE.md`
- `docs/VR.md`
- `docs/D3D8.md`
- `docs/D3D9.md`
- `docs/D3D10.md`
- `docs/MOTION_VECTORS.md`
- `docs/DEPTH.md`
- `docs/JITTER.md`
- `docs/HARNESS.md`
- `docs/CASE_STUDY_BIOSHOCK_VR.md`
- `docs/CASE_STUDY_ROGUE_TRADER_DLSS.md`
- `docs/REFERENCES.md`

## Strongest first-pass findings

1. Cross-bitness GPU transport is viable in principle and demonstrated upstream by DLSS5-Feeder for x86 clients with an x64 helper.
2. D3D10 needs distinct treatment; a private D3D11 relay is a demonstrated approach.
3. The central technical risk is producing correct temporal inputs, especially motion vectors and jitter, not merely invoking a modern backend.
4. Real Super Resolution requires control of internal render resolution and projection jitter; a finished-frame downscale/process/upscale path is not equivalent.
5. ReShade is valuable for prototyping and depth/API observation but is not selected as a mandatory dependency.
6. OptiScaler is most useful when a modern temporal-upscaler contract already exists; it does not replace the legacy data-extraction layer.
7. DLSS, FidelityFX temporal upscaling, and XeSS share core temporal concepts but have backend-specific inputs and conventions.

## Second-pass findings

1. DLSS5-Feeder stable remains `v0.15.1`; newest observed prerelease is `v1.16.0-beta.1` / `55c5bca` (2026-09-10). The x86/host64 pair uses IPC protocol v9 and must match, reinforcing explicit protocol/build-generation diagnostics.
2. Streamline's current programming guide requires 64-bit Windows for SL features; XeSS-SR requires Windows x64. The x64 host is therefore a reusable multi-backend capability, not just a Feeder/NVIDIA workaround.
3. XeSS SDK `v3.0.2` is current. D3D12 is the generic cross-vendor SR path; current D3D11 XeSS-SR support is Intel Arc-or-later only. XeSS 3 also exposes externally managed heap concepts relevant to modern resource ownership.
4. AMD FSR SDK `2.3.0` / commit `60f4ea81909200d8542eca14dccb2628b763a9a3` exposes a five-function FSR API through signed DLLs; its current backend-specific API path is documented for DirectX 12. Direct `dumpbin /headers` inspection now verifies every DLL under `Kits/FidelityFX/signedbin` as PE `0x8664 (x64)`, including loader and upscaler. The published v2.3.0 signed DX12 runtime therefore cannot load directly into x86.
5. Microsoft's D3D11 `OpenSharedResource` docs explicitly describe D3D9 -> D3D11 shared textures under strict restrictions. A separate Microsoft overview says unsynchronized sharing requires D3D9Ex and excludes D3D9c/older runtimes. This is now a concrete probe, not a fact to generalize.
6. NVIDIA RTX SDK license v. March 14 2024 was reviewed from the primary text. It permits covered SDK material incorporated in object-code form subject to requirements, disallows standalone SDK redistribution, and adds DLSS/NGX-specific hardware, attribution and commercial-release notification terms.
7. OpenXR 1.1 confirms per-view poses tied to a target `predictedDisplayTime`; VR validation should preserve/log that timing and view identity through the temporal pipeline.
8. AMD FidelityFX, Intel XeSS and ReShade primary licensing was reviewed at the architecture-research level. AMD permits covered binary redistribution subject to notices and no reverse engineering; Intel XeSS permits redistribution of unmodified binary software with notices and no reverse engineering; ReShade is BSD-3-Clause with key public API headers dual BSD-3-Clause/MIT. Exact selected files/binaries still require a release-time check.
9. The AMD `v2.3.0` signed DX12 DLL set has now been inspected at the PE-header level: loader, upscaler, frame generation, denoiser and radiance-cache DLLs all report `0x8664 (x64)`. This closes the v2.3.0 artifact question while remaining scoped to that release.
10. ReShade's current generic depth add-on proves API-specific depth strategies: D3D9 `INTZ` substitution where possible, D3D10/11 typeless/SRV access, backup-before-clear and explicit MSAA resolve handling. Depth selection itself remains heuristic and must carry provenance/override support.
11. LumeniteFX/Feeder provide a strong optical-flow baseline with confidence and validation masks, but current upstream reports fast-motion/thin-geometry/transparency/HUD artifacts. Renderer-derived motion remains the target; optical flow is fallback/baseline evidence.
12. ReShade's current D3D8 setup path points users to BSD-2-Clause `d3d8to9`. D3D8 should eventually compare native interception, d3d8to9->D3D9 and dgVoodoo2->D3D11/12 on the same target.
13. Feeder's synthetic post-process jitter experiment is useful negative evidence: shifting a downsample grid after rendering is not projection jitter and cannot provide real SR semantics or game-side performance gain.

## BioShock VR case-study findings

Pinned upstream: `Beren5556/BioShock-VR-DLSS-DLAA` release `v0.2.17-en`, source commit `8671fc87c4646140419ea64bd6e60d59fcac4723`. Full analysis is in `docs/CASE_STUDY_BIOSHOCK_VR.md`.

1. **Observed upstream:** BioShock Remastered is a real D3D11/x86 integration with separate x64 NGX helpers and fully independent per-eye pipes, resources, fences and histories. This strongly supports the current layered architecture and proves that view identity must survive through transport/backend execution.
2. **Verified from source:** BioShock 1 and BioShock 2 share backend-facing temporal types but implement independent game-specific camera/projection/depth providers. This is concrete evidence for keeping reusable mechanisms separate from per-game profiles/providers.
3. **Verified from source:** the current motion vectors are reconstructed from hardware depth plus current/previous camera state and use current-pixel -> previous-pixel, render-pixel units. They are camera-only; moving hands/weapons/enemies/particles/water do not have object vectors.
4. **Verified limitation:** BioShock 2 sends zero projection jitter for DLAA and SR and explicitly documents this as a temporal-quality limitation, especially for SR. Successful NGX evaluation therefore demonstrates a functioning partial temporal contract, not renderer-native/full-quality SR semantics.
5. **Verified/observed upstream:** BioShock 2 uses exact eye/build camera/projection identity, explicit reject reasons and aggressive history resets rather than a "latest camera" fallback. These diagnostics should influence LTR Bridge's controlled contract/harness.
6. **Verified:** upstream includes a game-independent synthetic x86 D3D11 -> two x64 host stereo test with deterministic per-eye readback and cross-eye contamination checks. LTR Bridge's planned x86/x64 probe should adopt those validation ideas without copying BioShock's IPC ABI.
7. **Observed upstream with qualification:** documented BS2 runs execute real NGX DLAA and multiple SR resolutions for thousands of frames through two eye hosts and an OpenXR simulator, but upstream explicitly does not claim final physical-headset image quality, complete object motion, sustained 90 Hz or a clean shutdown for those runs.
8. **Verified licensing/provenance:** the repository is MIT; its x64 host derives from DLSS5-Feeder/NIGos dlss5-bridge and keeps NVIDIA NGX under separate NVIDIA terms.

## Rogue Trader DLSS case-study findings

Pinned public reference: `BradyBrenot/RogueTrader_DLSS` release `v2.2` / commit `01b1cd816db08f2b1c6c68b1319c6f44dd61bdd6`; original baseline `cstamford/RogueTrader_DLSS@f2444b09ecee649e39851715cb5133e7020e159c`. Full analysis is in `docs/CASE_STUDY_ROGUE_TRADER_DLSS.md`.

1. **Verified from source:** the Waaagh/Unity render-graph integration reads native scene color, depth and motion vectors and forces both camera- and object-MV passes to remain active while replacing the game's AA/upscale stage.
2. **Verified from source:** the provider controls true scaled render extent vs display extent and injects real renderer projection jitter through the camera buffer. DLSS executes before full-resolution post-processing.
3. **Verified limitation:** native MV provenance is not equivalent to complete coverage. Current issue `#2` documents cloth/cape ghosting because cloth is missing from the motion-vector path. The provisional contract now keeps coverage/exclusions separate from provenance.
4. **Verified from fork history:** real SR exposed non-backend engine assumptions: particle/billboard sizing used the wrong screen-size constant and mip bias needed render/output-ratio correction. Real SR testing must include these resolution-dependent behaviors.
5. **Verified current architecture:** the 2026 fork removed process hooking/MinHook and now queues evaluation from managed code and executes it on Unity's render thread through `IssuePluginEventAndData`. Prefer the least-invasive engine-native render-thread mechanism when one exists; do not generalize it to legacy APIs that lack one.
6. **Observed gap:** NGX history reset is primarily tied to feature recreation rather than the rich temporal discontinuity/rejection policy seen in BioShock. The harness should combine Rogue Trader's renderer-native inputs with BioShock's temporal identity/reset discipline.
7. **Observed backend evidence:** the fork documents routing its DLSS input contract through OptiScaler for non-NVIDIA backends, supporting backend separation once coherent temporal inputs exist.
8. **Licensing caution:** no root license was detected in the reviewed Rogue Trader repositories. Treat code as reference-only unless explicit reuse terms are obtained; NVIDIA SDK/runtime terms remain separate.

## Candidate architecture

`Legacy API Adapter -> Temporal Data Provider -> optional Transport -> Modern Graphics Host -> Reconstruction Backend -> game/VR output`

Treat this as a hypothesis until the first probes are complete. The second pass strengthens D3D12 x64 as the first modern-host target, but does not make it a permanent universal requirement. The BioShock and Rogue Trader case studies independently strengthen the separation between shared semantic/transport/backend mechanisms and game-specific temporal providers while showing different fidelity points on the same temporal-quality ladder.

## Phase 1 harness state

1. **Implemented:** `ltr_temporal_harness` is a standalone x64 D3D11 research executable with 1:1 render/output extents, real renderer projection jitter, shader-readable hardware depth, and current-pixel -> previous-pixel ground-truth motion in render-pixel units.
2. **Implemented:** motion excludes projection jitter and records that convention separately. Coverage currently includes camera and rigid-object motion; skinned/cloth geometry, particles, transparency, and HUD are explicit known exclusions.
3. **Implemented:** frame identity includes a monotonic frame index, provisional view index, history generation, and explicit startup/resize/scenario/manual reset reason. This remains internal research metadata, not a public ABI.
4. **Host-tested:** Release x64 built with MSVC 19.44 / Windows SDK 10.0.26100.0. A hidden smoke run stayed alive and responsive for three seconds after D3D11 initialization.
5. **Implemented:** `--self-test` is registered in CTest and Windows CI. It renders reset/steady frames for static, camera translation, camera rotation, rigid-object motion, deforming geometry, masked particle, blended transparency, HUD overlay and dedicated disocclusion; reads depth and `R32G32_FLOAT` motion back through staging textures; validates reset/history/frame identity and expected motion; and emits a text report plus scene/depth/MV/history/reconstruction diagnostic BMPs. Passing local/CI executions are **host-tested** evidence.
6. **Verified:** the original pixel shader read `renderSize` from `PerDraw`, but that constant buffer had only been bound to VS slot `b0`, so all MV values were multiplied by zero. `PerDraw` is now bound to both VS and PS. A second test failure revealed the static reference geometry was culled by winding; the winding was corrected instead of weakening the validation criterion.
7. **Visual-validated:** generated depth/MV diagnostics were inspected after a passing local CTest run. Static MV is neutral; camera translation/rotation show continuous depth-dependent fields; rigid-object motion is spatially isolated from the static background. This claim is limited to the current controlled ground-truth cases.
8. **Implemented:** the harness now includes a separable camera+depth provider. It reconstructs current world position from hardware depth using the inverse current jittered VP, reprojects through current/previous unjittered camera VP, writes `R32G32_FLOAT` current-pixel -> previous-pixel motion, and exposes a reconstructed-motion debug view. It deliberately has camera-only coverage; object transforms are not supplied.
9. **Host-tested:** deterministic readback compares camera+depth with renderer ground truth. Translation measures 0.000404 px mean / 0.001781 px max error; rotation measures 0.000811 px mean / 0.004889 px max error. Static/reset cases remain effectively zero. The rigid-object scenario asserts the expected limitation: renderer ground truth marks 20,686 moving pixels, reconstructed motion marks zero, and `camera_only_miss` is exactly 20,686.
10. **Visual-validated:** reconstructed-motion/error BMPs were inspected. Camera translation/rotation are continuous with effectively zero error; static reconstruction is neutral; rigid-object error is isolated to the moving object while the static floor remains zero. This does not promote camera+depth to object-motion coverage.
11. **Implemented:** visible draws now emit stable per-surface identity. The self-test reprojects each current active pixel with renderer ground-truth MV plus the previous/current jitter delta and checks whether it maps in-bounds to the same previous surface. For the current controlled planar surfaces, this produces an explicit ground-truth history-validity/disocclusion oracle independent of the camera+depth provider; broader geometry will require stronger identity/depth tests.
12. **Host-tested:** reset frames reject all active history; static steady keeps 232,423/232,455 pixels valid; camera translation 221,639/223,941; camera rotation 215,564/219,379. Rigid-object motion marks 13,375 background pixels as disoccluded; the dedicated disocclusion scenario marks 3,059. CTest now enforces reset invalidation, >99.9% static validity, >95% camera validity, and >1% newly revealed background for the object/disocclusion cases.
13. **Visual-validated:** history-validity BMPs were inspected. The dedicated case rejects the floor region uncovered by object translation, the rigid case rejects the larger revealed region from translation+rotation, and static is valid apart from a minimal raster/jitter boundary.
14. **Implemented:** `src/optical_flow` is a separate CPU/synthetic image-space baseline. It generates textured current/previous frame pairs with known current->previous truth, runs hierarchical luminance block matching from a 1/64 pyramid to full resolution on a 1/8 output grid, and computes confidence from match cost, local texture and uniqueness. Renderer depth, matrices and surface identity are not inputs to the estimator; truth/invalid masks are evaluation-only.
15. **Host-tested:** the standalone and root-integrated probe passes deterministic static, camera-translation, camera-rotation, rigid-object and disocclusion scenarios. <=4 px valid accuracy is 100.000%, 78.919%, 84.196%, 95.675% and 96.927% respectively; confident-valid accuracy is 100.000%, 90.302%, 90.263%, 98.706% and 99.449%. Camera translation remains the weakest controlled case at 4.580 px confident mean error. Confidence separates valid from invalid samples in the moving/disocclusion cases.
16. **Visual-validated:** the controlled optical-flow disocclusion diagnostics localize the invalid/revealed region and reduce confidence there. Camera translation still contains isolated/border errors. This claim is limited to the generated synthetic diagnostics and is not evidence of production optical-flow quality, game support or runtime performance.
17. **Verified:** current LumeniteFX was inspected at `umar-afzaal/LumeniteFX@f8cbbb4eccfcb7adf0d74bb358ba349272e3c1e9` (`https://github.com/umar-afzaal/LumeniteFX`). Its optical-flow path is substantially more complex than this first probe, using pyramidal matching, refinement and temporal confidence; its NOTICE cites FidelityFX Optical Flow 1.1.2, Zenteon (August 2025) and VPP as studied/influential sources. LTR Bridge keeps it as an external reference and does not copy that implementation into the baseline.
18. **Implemented:** the D3D11 harness now includes a `deforming-geometry` scenario. The scene VS receives distinct current/previous procedural deformation phases and computes ground-truth motion from deformed current/previous vertex positions. This is deliberately a small non-rigid proxy; it is not evidence for arbitrary engine skeletal animation or cloth simulation.
19. **Host-tested:** the deforming steady frame reports 36,725 moving pixels; camera+depth reports zero reconstructed moving pixels and 36,716 `camera_only_miss` pixels. History validity keeps 234,518/235,459 active pixels valid and rejects 941, including 799 newly revealed background pixels. The deformation-specific disocclusion criterion remains enforced in the full harness test.
20. **Visual-validated:** the deforming ground-truth MV map varies spatially inside the primitive, the camera+depth error map localizes the missing non-rigid motion to that primitive, and the history-validity map marks the narrow revealed regions while preserving the rest of the surface/background.
21. **Implemented:** the harness now includes a `masked-particle` alpha-test proxy. A translating primitive uses a deterministic procedural cutout, so each surviving fragment still has one unambiguous surface identity and renderer MV. `coverage_particles` remains a declared exclusion because this is not a real particle system.
22. **Host-tested:** the masked-particle steady frame reports 6,838 moving pixels; camera+depth reconstructs zero moving pixels and reports 6,838 `camera_only_miss` pixels. History validity keeps 223,634/225,775 active pixels valid, rejects 2,141 and identifies 1,966 newly revealed background pixels. A scenario-specific >0.5% revealed-background criterion is used because the sparse cutout coverage is intentionally smaller than the rigid-object case.
23. **Visual-validated:** masked-particle diagnostics show motion only on the procedural cutout fragments, camera+depth error on those same fragments, and rejected background at the previous particle locations while the current fragments remain valid.
24. **Implemented:** `blended-transparency` draws the floor first, then a 45%-opacity moving layer with color blending, foreground MV/surface identity, depth testing and no depth writes. The history readback records mixed-layer pixels separately. General `coverage_transparency` remains excluded.
25. **Host-tested:** the transparency steady frame has 7,399 mixed pixels carrying foreground motion; camera+depth reconstructs zero moving pixels and reports 7,399 `camera_only_miss` pixels because depth remains the background. History validity keeps 208,162/220,523 active pixels valid overall; among the mixed pixels it accepts 3,608 and rejects 3,791, while 7,290 background pixels are newly exposed. The full eight-scenario harness self-test returns `RESULT PASS`.
26. **Visual-validated:** the scene diagnostic visibly composites the transparent triangle over the floor. The motion diagnostic assigns foreground motion to the mixed pixels, and the validity diagnostic shows both accepted current-layer samples and rejected/revealed regions.
27. **Observed:** in this controlled mixed-layer case, scene color contains foreground and background contributions with different temporal behavior, while the current single MV/surface slot describes only one contributor and depth describes the other. This is evidence that a universal one-vector/one-identity model is insufficient for general blended transparency; no reactive-mask, composition-mask or other mitigation is selected yet.
28. **Implemented:** `hud-overlay` draws a moving screen-space rectangle after scene rendering and writes scene color only; depth, renderer MV and surface identity are intentionally untouched. The history readback separately counts HUD pixels whose overlay membership differs from the reprojected previous location. General `coverage_hud` remains excluded.
29. **Host-tested:** the HUD steady frame has 11,886 overlay pixels; all 11,886 have changed HUD history and all 11,886 are nevertheless classified valid by the depth/MV/surface oracle. Geometry motion and camera+depth reconstruction both remain zero. The full nine-scenario harness self-test returns `RESULT PASS`.
30. **Visual-validated:** the scene diagnostic shows the white screen-space rectangle over the floor while the history-validity diagnostic remains green over the same geometry region.
31. **Observed:** independently changing post-scene HUD/highlight content is invisible to a geometry-only depth/MV/surface validity contract. This complements the mixed-transparency result and makes an explicit content/reactive classification path a backend-mapping requirement to investigate, not yet a selected universal mitigation.
32. **Implemented/host-tested:** an optional D3D12 x64 probe now runs Intel XeSS SDK `v3.0.2` / commit `8fe81bdbbaf00b3c1b733fd0d830c333dc84e6f0` in `XESS_QUALITY_SETTING_AA`. The SDK is external and not vendored. XeSS reports `256x144 -> 256x144`; the probe now shares the harness's 8-sample Halton jitter sequence and supplies high-resolution, one-pixel-dilated current-to-previous render-pixel MV with jitter excluded, plus explicit frame-0 history reset and GPU readback.
33. **Observed:** a second XeSS context enables `XESS_INIT_FLAG_RESPONSIVE_PIXEL_MASK` and marks only pixels whose synthetic moving-HUD membership changes. On the local RTX 4070 Ti / driver `32.0.16.1692`, final changed-region RGB MAE fell from `8.3259` baseline to `5.6902` with the responsive mask, and output hashes differed. This is the first real backend mapping of the HUD/content-classification requirement under coherent jitter/MV semantics; it is not a universal HUD policy or visual-quality validation.
34. **Host-tested:** the XeSS-enabled build passed all three tests: XeSS Native AA probe, D3D11 temporal harness self-test and independent optical-flow probe. GitHub CI remains SDK/GPU-neutral because the XeSS target is opt-in.
35. **Host-tested:** D3D12 timestamp queries around `xessD3D12Execute` after two warm-up frames measured baseline mean/min/max `0.1886/0.1874/0.1894 ms` and responsive mean/min/max `0.1895/0.1884/0.1905 ms` at `256x144`. `xessGetProperties` reported `65,536` bytes temp-buffer plus `1,835,008` bytes temp-texture heap per context (`1.8125 MiB`). These numbers are single-host, low-resolution evidence only and are not `performance-validated`.
36. **Experiment-pending:** real skinned/cloth content, real particle systems, real-engine/general transparency/HUD policy, real-content optical-flow validation, direct renderer-resource transfer, repeated/resolution-scaled performance and memory measurement, additional backend mappings, SR, and stereo.

## Next concrete work

1. Re-inspect repository state and this handoff.
2. Continue Phase 1 from the validated renderer-ground-truth, camera+depth, optical-flow and coherent-jitter/MV XeSS Native AA baselines. Before the x86 bridge, expand the XeSS measurement across useful resolutions/repeated runs and preserve the distinction between SDK temporary-heap requirements and total VRAM use.
3. AMD FSR `v2.3.0` signed DX12 binaries are verified x64. Re-check the exact artifacts if a different AMD SDK release is selected. Primary license terms for NVIDIA, AMD, Intel, ReShade and dgVoodoo2 are already recorded; re-check exact selected components before shipping.
4. Before a large D3D9 experiment, build the smallest possible classic-D3D9/D3D9Ex -> D3D11 shared-texture probe to resolve the Microsoft-documentation ambiguity and measure synchronization/copy behavior.
5. The HUD/highlight requirement now has one concrete XeSS responsive-mask mapping. Preserve that as backend-specific evidence; transparency remains separately unresolved, and no universal reactive/composition policy is selected. Keep real skinned/cloth and particle systems explicitly pending before treating any provider as broadly representative.
6. Use the independent optical-flow baseline beside ground truth and camera+depth motion to quantify quality loss; the first coherent jitter/MV Native AA path now exists, so the next evidence should focus on broader resolution/content coverage rather than another zero-jitter bootstrap.
7. Build a backend-neutral D3D11 x86 -> D3D12 x64 round-trip resource-sharing probe. From the first stereo-capable version, include two independent streams, distinct histories/resources, deterministic readback and cross-eye contamination detection.
8. Reproduce the D3D10 relay and then compare D3D9 native dedicated transport, D3D9/D3D9Ex -> D3D11 relay, and dgVoodoo2 translation on the same target.
9. Once D3D9 is understood, compare D3D8 native interception, d3d8to9->D3D9 and dgVoodoo2->modern paths on one controlled scene.
10. When moving from native-AA to true SR, add a render-stage/resolution-assumption matrix covering post-processing, HUD/highlights, particles/billboards, mip bias and secondary-camera/pass behavior.

## Important upstream snapshot

- BioShock VR DLSS/DLAA case study: `v0.2.17-en` / `8671fc87c4646140419ea64bd6e60d59fcac4723`.
- Rogue Trader EnhancedGraphics case study: public fork `v2.2` / `01b1cd816db08f2b1c6c68b1319c6f44dd61bdd6` (original baseline `f2444b09ecee649e39851715cb5133e7020e159c`).
- DLSS5-Feeder stable observed: `v0.15.1` / `3f62485`.
- DLSS5-Feeder newest prerelease observed: `v1.16.0-beta.1` / `55c5bca`.
- OptiScaler stable observed: `v0.9.4` / `7534ad0`.
- XeSS SDK current observed: `v3.0.2` / `8fe81bd`.
- AMD FSR SDK current observed: `2.3.0`.
- ReShade ecosystem version observed: 6.8.0; GitHub commit history previously seen through 2026-09-10 (`a33de92` shown for that date).
- LumeniteFX optical-flow reference inspected: `f8cbbb4eccfcb7adf0d74bb358ba349272e3c1e9`.

## Repository publication

- `origin` is `https://github.com/rubocopter/LTR-Bridge.git` on `main`.
- The research/bootstrap, second research pass, BioShock VR case study and Rogue Trader DLSS case study are published to GitHub.
- Before the Rogue Trader research edits, the local checkout at `E:\LTR_bridge` was reconciled and fast-forwarded to `4376f7bd41f67a5863fc749f26491ebd1117818f`; the stale local stash was resolved and removed, and the working tree was clean before this pass.

## Do not do next

Do not begin a production injector, do not modify existing game-mod repositories, and do not freeze a public universal temporal ABI before at least DLSS/DLAA, FidelityFX and XeSS mappings plus one x86 transport and one legacy API experiment have informed it. Do not treat BioShock's D3D11/game-specific provider as evidence that D3D8/9/10 or another engine can use the same hooks or transport unchanged.
