# Codex handoff

Updated: 2026-09-13.

## Repository state

This workspace started empty. The project remains in research/design mode; there is no production injector or reconstruction runtime.

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
4. AMD FSR SDK `2.3.0` exposes a five-function FSR API through signed DLLs; its current backend-specific API path is documented for DirectX 12. Exact x86 binary availability remains unverified from PE headers.
5. Microsoft's D3D11 `OpenSharedResource` docs explicitly describe D3D9 -> D3D11 shared textures under strict restrictions. A separate Microsoft overview says unsynchronized sharing requires D3D9Ex and excludes D3D9c/older runtimes. This is now a concrete probe, not a fact to generalize.
6. NVIDIA RTX SDK license v. March 14 2024 was reviewed from the primary text. It permits covered SDK material incorporated in object-code form subject to requirements, disallows standalone SDK redistribution, and adds DLSS/NGX-specific hardware, attribution and commercial-release notification terms.
7. OpenXR 1.1 confirms per-view poses tied to a target `predictedDisplayTime`; VR validation should preserve/log that timing and view identity through the temporal pipeline.
8. AMD FidelityFX, Intel XeSS and ReShade primary licensing was reviewed at the architecture-research level. AMD permits covered binary redistribution subject to notices and no reverse engineering; Intel XeSS permits redistribution of unmodified binary software with notices and no reverse engineering; ReShade is BSD-3-Clause with key public API headers dual BSD-3-Clause/MIT. Exact selected files/binaries still require a release-time check.
9. The current AMD `signedbin` tree contains one DX12 DLL set without architecture-specific filenames. That is insufficient evidence of x86 or x64 machine type, so FSR CPU-architecture support remains explicitly unverified until the PE headers are inspected.
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

## Next concrete work

1. Re-inspect repository state and this handoff.
2. Before implementing the D3D11 harness, mine the two real-game case studies only for reusable validation ideas: BioShock for exact identity/reject/reset/stereo isolation and camera+depth motion; Rogue Trader for renderer-native MV coverage, real jitter, render/output extent control and insertion-point/resolution-assumption tests. Do not import game-specific hooks or freeze either implementation's ABI.
3. When direct binary inspection is available, inspect the PE machine type of the current AMD FSR signed loader/upscaler DLLs. Do not infer x86/x64 support from filenames. Primary license terms for NVIDIA, AMD, Intel, ReShade and dgVoodoo2 are already recorded; re-check exact selected components before shipping.
4. Before a large D3D9 experiment, build the smallest possible classic-D3D9/D3D9Ex -> D3D11 shared-texture probe to resolve the Microsoft-documentation ambiguity and measure synchronization/copy behavior.
5. Build a controlled D3D11 x64 temporal harness. Prefer native-resolution temporal AA first so color/depth/MV/jitter/history can be validated without internal-resolution changes. Compare native camera+object motion, camera+depth reconstruction and optical flow; carry motion coverage/exclusions separately from provenance.
6. Add diagnostic visualizations for the exact depth resource/convention, motion direction/scale/confidence/validity and history reset. Include an optical-flow baseline beside ground-truth/renderer-derived motion so quality loss is measurable rather than anecdotal.
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

## Repository publication

- `origin` is `https://github.com/rubocopter/LTR-Bridge.git` on `main`.
- The research/bootstrap, second research pass and BioShock VR case study are published to GitHub; the Rogue Trader case-study changes are part of the current research pass.
- Before the Rogue Trader research edits, the local checkout at `E:\LTR_bridge` was reconciled and fast-forwarded to `4376f7bd41f67a5863fc749f26491ebd1117818f`; the stale local stash was resolved and removed, and the working tree was clean before this pass.

## Do not do next

Do not begin a production injector, do not modify existing game-mod repositories, and do not freeze a public universal temporal ABI before at least DLSS/DLAA, FidelityFX and XeSS mappings plus one x86 transport and one legacy API experiment have informed it. Do not treat BioShock's D3D11/game-specific provider as evidence that D3D8/9/10 or another engine can use the same hooks or transport unchanged.
