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
- [ ] Pin additional upstream source commits where conclusions currently rely on moving `main` branches.
- [ ] Complete a licensing/redistribution review from primary license texts for every candidate dependency.

## Phase 1 — D3D11 x64 temporal harness

Status: **planned**.

Goal: prove temporal correctness without legacy transport complexity.

Success criteria:

- controlled color, depth, motion vectors, and projection jitter;
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

Compare two routes on the same controlled target:

1. native D3D9 interception plus a relay/transport designed for D3D9;
2. dgVoodoo2 D3D9 -> D3D11 followed by the modern path.

Collect compatibility, depth access, transform/MV visibility, proxy coexistence, GPU copies, latency, frame pacing, device reset behavior, and mod/VR integration impact.

## Phase 6 — first real legacy game

Status: **planned**.

Choose a title only after Phases 1–5 establish which architecture is justified. A game is a validation target, not the place to invent the basic transport contract.

## Phase 7 — controlled stereo/OpenXR harness

Status: **planned**.

Two independent eye targets, histories, matrices, depth and MVs. Measure temporal divergence, latency, and submission order before trying a real VR mod.

## Phase 8 — VR mod validation

Status: **planned**.

Integrate only as a consumer/case study. Require `vr-headset-validated` evidence before claiming VR support.

## Promotion policy

No row in `docs/COMPATIBILITY.md` becomes `supported` from compilation, a synthetic transport test, or one screenshot. Promotion requires the relevant runtime, visual, performance, and when applicable headset validation states.
