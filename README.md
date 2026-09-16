# LTR Bridge — Legacy Temporal Reconstruction Bridge

LTR Bridge is an independent research project exploring whether modern temporal reconstruction and antialiasing techniques can be applied to legacy renderers, including eventual use in VR mods.

The primary research targets are DLSS Super Resolution, DLAA, FSR-family temporal upscalers, and XeSS. The source renderers of interest begin with Direct3D 8, 9, 10, and 11, with both x86 and x64 applications in scope.

This repository is not part of any existing VR mod. Existing mods may become future consumers or validation cases, but they are not dependencies and must not be modified from this project.

## Current conclusion

The first research pass supports a **candidate** layered design:

`legacy renderer -> API adapter -> temporal data provider -> optional transport -> modern host -> reconstruction backend -> game/VR output`

That shape is plausible but not yet validated as a universal architecture. The cross-bitness GPU-resource pattern first identified in DLSS5-Feeder is now independently host-tested here: a 32-bit D3D11 client can keep frame data on the GPU while a 64-bit D3D12 helper executes modern work, with live resource replacement, bounded queueing, failure handling and synthetic per-eye isolation. The same modern transport is also reached from current-host D3D9Ex and D3D10.1 relay paths through a private x86 D3D11 conversion step.

The largest unresolved problem is not calling an upscaler. It is producing trustworthy temporal inputs from engines that were never designed for them, especially dense motion vectors, correct depth, projection jitter, history resets, and independent per-eye state for VR.

## Initial prototype direction

The controlled D3D11 x64 harness foundation is implemented and host-tested. It renders known 1:1 scenarios with projection jitter, shader-readable depth, renderer-ground-truth motion, explicit history resets, provisional view identity, content/history diagnostics, camera+depth reconstruction and an independent synthetic optical-flow baseline.

The first optional real backend probe is also implemented: XeSS SDK 3.0.2 Native AA executes at 1.0x on D3D12 x64 against deterministic synthetic inputs. It now shares the harness's Halton jitter sequence and motion convention (current-to-previous render-pixel MV with jitter excluded), exercises XeSS's responsive-pixel mask, and records first-host GPU timestamp plus temporary-heap measurements. Repeated Native AA measurements cover 256x144 through 3840x2160 on the same host. The cross-bitness transport probe now runs 24 verified frames across a live `64x64 -> 96x72` resource replacement: generation 1 is created only after generation 0 completes, its handle is duplicated into the already-running x86 process, and a small control channel carries the replacement contract. It uses separate D3D11-ready/D3D12-done fences, records first timing/copy measurements and exercises protocol, adapter, resource-contract, host-stall, malformed dynamic-control, abrupt client termination, controlled D3D12 device removal and abrupt host-termination paths, including host loss while a depth-1 backpressure slot is awaiting reuse. A separate fixed `64x64` backpressure probe validates pre-created rings of depth 1 and 2 over 24 frames with zero corruption and explicit slot-reuse waits. Controlled stereo modes now validate two independent `64x64` eye resources, separate ready/done fences, deterministic per-eye payloads, cross-eye contamination detection, and independent host-side GPU history with explicit reset semantics. An isolated x64 OpenXR bootstrap probe also negotiates with the active runtime and records runtime/system/view/graphics-requirement state without coupling OpenXR to the transport layer. This remains synthetic transport/history and host-runtime evidence only, not an OpenXR presentation, reconstruction-backend, headset, or performance validation. These remain controlled host results, not general support or performance validation.

The bridge now also has a renderer-transfer matrix on the x86 side. It covers same-format `CopyResource` through 4K, a 4x-MSAA `ResolveSubresource` path, and a fullscreen conversion from local `R10G10B10A2_UNORM` into the shared RGBA8 transport resource. All three larger profiles preserve live resource replacement and deterministic end-to-end validation. These are controlled single-host transfer measurements, not representative engine or VR performance results.

The first legacy-API relay route is now host-tested end to end. A Win32 probe compares classic D3D9 and D3D9Ex sharing into D3D11 on the same adapter: classic D3D9 rejects the tested shared-texture creations, while D3D9Ex shares R10G10B10A2 and RGBA16F resources; a BGRA8 control also works on this driver even though it is outside the documented D3D9 -> D3D11 format list. The integrated D3D9Ex path reaches the x64 D3D12 host through `StretchRect`, an event query, private x86 D3D11 conversion and the existing RGBA8 transport. A separate controlled scene probe adds engine-style vertex buffers, a D24S8 depth surface, fixed-function world/view/projection state and overlapping depth-tested draws. A further external-process probe now puts that scene behind a separate interceptor DLL: the target loads the DLL before renderer creation, the DLL patches the target's `Direct3DCreate9Ex` import and the resulting D3D9Ex COM vtables, then captures after `EndScene` without receiving renderer resources from the target. Five repeated `640x360 -> 1280x720` runs survive `ResetEx`, preserve the target's color/depth/world state and validate depth occlusion with zero mismatches. This is a controlled external interception boundary, not a generic injector or real-game integration.

The same controlled target has now been compared with official dgVoodoo2 2.87.5. The native D3D9Ex shared-relay design does not traverse the wrapper unchanged: the R10 scene target is rejected with `D3DERR_INVALIDCALL`, while the BGRA8 profile renders but the interceptor's shared D3D9Ex relay creation is rejected. A separate probe using dgVoodoo's official D3D12 addon API is host-tested as a presentation boundary instead. Five `d3d12_fl11_0` runs observe the D3D12 root/device lifecycle, two `640x360 -> 1280x720` swapchain generations and 12/12 `PresentBegin`/`PresentEnd` callbacks with non-null source/destination resources. That callback exposes translated presentation resources, not the original bound D3D9 depth/world state seen by the native `EndScene` hook, so the two routes remain distinct research boundaries.

The D3D10 relay idea is now reproduced locally as well. A Win32 D3D10.1 device at feature level 10.0 copies a local R10 render target into a legacy shared R10 texture, waits for a D3D10 event query, and a same-adapter private D3D11 device opens that texture. The standalone probe survives `640x360 -> 1280x720` resource recreation with zero mismatches, and the integrated path feeds the existing RGBA8 x86/x64 bridge through the same D3D11 fullscreen conversion. Five repeated 24-frame bridge runs completed with zero mismatches. On this RTX 4070 Ti host, attempting the D3D10 keyed-mutex resource path returns `E_INVALIDARG`, so event-query synchronization is the locally proven route. This remains host/driver-scoped transport evidence; depth preservation, an SM4 temporal-data provider and real-game interception remain pending.

This order is a research hypothesis, not a permanent product architecture.

## Documentation

- [ARCHITECTURE.md](ARCHITECTURE.md) — candidate architecture and decision gates.
- [ROADMAP.md](ROADMAP.md) — staged research and prototype plan.
- [docs/RESEARCH.md](docs/RESEARCH.md) — research findings and evidence status.
- [docs/COMPATIBILITY.md](docs/COMPATIBILITY.md) — evidence-based compatibility matrix.
- [docs/TEMPORAL_CONTRACT.md](docs/TEMPORAL_CONTRACT.md) — provisional temporal contract analysis.
- [docs/X86_X64_BRIDGE.md](docs/X86_X64_BRIDGE.md) — cross-bitness resource/synchronization findings.
- [docs/VR.md](docs/VR.md) — VR constraints and per-eye requirements.
- [docs/D3D8.md](docs/D3D8.md), [docs/D3D9.md](docs/D3D9.md) and [docs/D3D10.md](docs/D3D10.md) — API-specific research.
- [docs/MOTION_VECTORS.md](docs/MOTION_VECTORS.md) — motion-vector strategies and gaps.
- [docs/DEPTH.md](docs/DEPTH.md) — depth discovery, preservation and semantic requirements.
- [docs/JITTER.md](docs/JITTER.md) — projection-jitter requirements and legacy injection constraints.
- [docs/HARNESS.md](docs/HARNESS.md) — controlled D3D11 x64 Phase 1 harness and evidence boundary.
- [docs/XESS_NATIVE_AA_PROBE.md](docs/XESS_NATIVE_AA_PROBE.md) — optional XeSS 3.0.2 D3D12 Native AA execution and responsive-mask probe.
- [docs/OPENXR_RUNTIME_PROBE.md](docs/OPENXR_RUNTIME_PROBE.md) — isolated OpenXR runtime/system/view/graphics-requirement bootstrap probe.
- [docs/CASE_STUDY_BIOSHOCK_VR.md](docs/CASE_STUDY_BIOSHOCK_VR.md) — x86/x64 stereo transport and game-specific temporal-provider case study.
- [docs/CASE_STUDY_ROGUE_TRADER_DLSS.md](docs/CASE_STUDY_ROGUE_TRADER_DLSS.md) — renderer-native resolution/jitter/MV integration case study.
- [docs/CASE_STUDY_OFXR_BRIDGE.md](docs/CASE_STUDY_OFXR_BRIDGE.md) — OpenXR synthetic-frame presentation, stereo resource lifetime and pacing case study.
- [docs/REFERENCES.md](docs/REFERENCES.md) — research snapshot and source versions.

## Validation vocabulary

Repository claims use explicit maturity states: `planned`, `implemented`, `host-tested`, `live-tested`, `visual-validated`, `performance-validated`, `vr-headset-validated`, and `supported`.

As of 2026-09-16 the repository contains the controlled D3D11 x64 research harness, independent optical-flow baseline, a host-tested XeSS Native AA probe, a host-tested multiframe D3D11 x86 -> D3D12 x64 transport probe, current-host D3D9Ex and D3D10.1 -> D3D11 -> modern-host relays, a controlled external-process D3D9Ex interception probe, a host-tested dgVoodoo2 2.87.5 D3D12-addon presentation probe, and an isolated host-tested OpenXR runtime/bootstrap probe. No production injector, real-game integration, OpenXR presentation path, physical-headset validation, or performance-validated support claim is implemented.

## License

LTR Bridge's original code and documentation are released under the [MIT License](LICENSE). Third-party SDKs, runtimes, tools, and redistributed components retain their own licenses and distribution terms; see [docs/REFERENCES.md](docs/REFERENCES.md).
