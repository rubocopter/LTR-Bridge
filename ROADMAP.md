# Roadmap

The roadmap is evidence-driven. Items must be rechecked against repository state before work begins.

## Phase 0 — research baseline

Status: **implemented (documentation), not experimentally validated**.

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
- [ ] Pin additional upstream source commits where conclusions currently rely on moving `main` branches.
- [x] Review current primary distribution/license texts for AMD FidelityFX signed SDK binaries, Intel XeSS SDK, and ReShade/API headers.
- [ ] Re-check the exact backend binaries, source files and third-party notices selected for packaging immediately before any release.

## Phase 1 — D3D11 x64 temporal harness

Status: **planned**.

Goal: prove temporal correctness without legacy transport complexity.

Success criteria:

- controlled color, depth, motion vectors, and projection jitter;
- compare renderer-native/ground-truth motion against an image-space optical-flow baseline and preserve confidence/validity diagnostics;
- explicit history reset;
- 1:1 native-resolution temporal AA path first;
- debug views that validate depth/MV direction and scale;
- visual validation on static camera, translation, rotation, animated geometry, particles, HUD, resize, and history reset;
- frame-time and VRAM measurements.

Decision gate: only proceed to legacy integration after the contract can be validated independently of a game.

## Phase 2 — D3D11 x86 -> x64 bridge probe

Status: **planned**.

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

## Phase 3 — backend comparison

Status: **planned**.

Run the controlled contract through at least DLAA/DLSS-compatible integration, FidelityFX temporal upscaling, and XeSS Native AA/SR where the available SDK/API path permits it. Document non-common inputs instead of hiding them.

## Phase 4 — D3D10 probe

Status: **planned**.

Reproduce the useful architectural idea demonstrated by current DLSS5-Feeder: D3D10 game device -> legacy shared texture -> private D3D11 relay -> modern shared-resource path. Test whether this is robust beyond one implementation and one title.

Also test Shader Model 4-compatible MV reconstruction paths and compare event-query synchronization with any keyed-mutex support actually observed on test hardware.

## Phase 5 — D3D9 architecture comparison

Status: **planned**.

Start with a minimal API-interop probe before attempting reconstruction:

- classic D3D9 producer -> documented shared texture -> D3D11 consumer;
- D3D9Ex producer -> the same D3D11 consumer;
- verify allowed formats, pixel contents, synchronization, resize/recreation, OS/driver behavior and whether the classic-D3D9 case is genuinely supported.

Then compare three routes on the same controlled target:

1. native D3D9 interception plus a dedicated transport;
2. native D3D9/D3D9Ex interception -> constrained shared-texture D3D11 relay -> modern path, when supported;
3. dgVoodoo2 D3D9 -> D3D11 followed by the modern path.

Collect compatibility, depth access, transform/MV visibility, proxy coexistence, GPU copies, latency, frame pacing, device reset behavior, and mod/VR integration impact.

## Phase 6 — first real legacy game

Status: **planned**.

Choose a title only after Phases 1–5 establish which architecture is justified. A game is a validation target, not the place to invent the basic transport contract.

## D3D8 comparison — after the D3D9 boundary is understood

Status: **planned**.

Use the same controlled D3D8 target to compare native interception, `d3d8to9` followed by the proven D3D9 route, and dgVoodoo2 followed by the modern route. The decision should include renderer-state visibility, depth/MV opportunity, compatibility, proxy coexistence, redistribution and performance.

## Phase 7 — controlled stereo/OpenXR harness

Status: **planned**.

Two independent eye targets, histories, matrices, depth and MVs. Measure temporal divergence, latency, and submission order before trying a real VR mod.

## Phase 8 — VR mod validation

Status: **planned**.

Integrate only as a consumer/case study. Require `vr-headset-validated` evidence before claiming VR support.

## Promotion policy

No row in `docs/COMPATIBILITY.md` becomes `supported` from compilation, a synthetic transport test, or one screenshot. Promotion requires the relevant runtime, visual, performance, and when applicable headset validation states.
