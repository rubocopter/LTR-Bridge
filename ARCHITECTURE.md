# Candidate architecture

Status: **hypothesis informed by current evidence**.

The architecture should preserve four boundaries until experiments show that combining them is beneficial: source-renderer observation, temporal-data production, transport, and reconstruction execution.

The current research has proved each boundary in isolation far enough that the next risk is integration rather than another horizontal capability probe. Probe implementations are evidence generators, not production components: validation readbacks, synchronous logging, global hook state, target-fatal assertions and proof-oriented waits must not be copied unchanged into a real renderer path.

## 1. Legacy API Adapter

Responsibilities:

- observe device/swap-chain creation, resets, presents, render targets, depth targets, shaders, constants, and relevant draw state;
- identify frame boundaries and the scene color that should enter temporal reconstruction;
- expose enough renderer data for a temporal provider;
- prepare resources for transport when required.

Likely implementations are API-specific rather than one universal hook: D3D8, D3D9, D3D10, and D3D11 have materially different sharing and synchronization capabilities.

## 2. Temporal Data Provider

Produces a provisional `TemporalFrameInput` from renderer evidence. Providers may use native velocity buffers, intercepted transforms, depth-plus-matrix reconstruction, or a documented fallback.

This layer owns the semantic problem: whether motion, depth, jitter, exposure, and history are correct. The API adapter should not pretend that a texture is a valid motion-vector buffer merely because it can access it.

## 3. Transport

Optional. Flat x64 D3D11 may execute a backend in process; x86 applications may require an x64 helper.

Candidate responsibilities:

- process negotiation and versioning;
- GPU resource sharing or GPU copies;
- shared synchronization objects where available;
- control IPC and failure reporting;
- resize/device-loss/session rebuilds;
- backpressure and bounded buffering.

The design must distinguish resource sharing from zero-copy. Sharing a resource handle can still involve GPU copies before or after the shared resource.

## 4. Modern Graphics Host

Optional process or in-process context that owns the API required by a reconstruction backend. D3D12 is a strong initial host candidate because current NVIDIA integrations and proven x86-to-x64 bridges use it, but this is not yet a universal requirement.

The host should consume a versioned transport contract and should not need game-specific knowledge.

## 5. Reconstruction Backend

Backend adapters should express their real contracts rather than being forced behind a lowest-common-denominator interface too early.

The first comparison set is:

- DLSS / DLAA through current NVIDIA-supported integration paths;
- FidelityFX temporal upscaling;
- XeSS-SR / native anti-aliasing.

All three families require temporal information, but optional resources, conventions, resource formats, and API support differ.

## 6. VR Integration Layer

Optional and independent of non-VR operation. It must support temporal state per eye before OpenXR submission and must not require processing a final stereo-composited backbuffer.

Conceptually:

`TemporalFrameInput[eye] -> backend history[eye] -> reconstructed eye image -> OpenXR submission`

## Candidate data flow

```text
Game / legacy renderer
        |
        v
Legacy API Adapter
        |
        v
Temporal Data Provider
        |
        +---- in-process backend when viable
        |
        v
Transport (optional, e.g. x86 -> x64)
        |
        v
Modern Graphics Host
        |
        v
Reconstruction Backend
        |
        v
Game output or per-eye VR submission
```

## Decision gates

The following decisions remain deliberately open:

| Decision | Evidence required before committing |
| --- | --- |
| ReShade as a required layer | At least two source APIs and one non-ReShade path compared for access, coexistence, and latency. |
| dgVoodoo2 as default legacy route | The controlled comparison is complete. Compare native D3D9Ex observation and dgVoodoo2 on the same real game for original depth/transform/MV provenance, compatibility, scheduling and frame-time behavior; the proven D3D12 addon presentation callback alone is not temporal-input evidence. |
| D3D12 as universal host | Backend/API matrix showing another host cannot provide equivalent capability or materially lowers portability. |
| Single universal backend interface | DLSS, FSR, and XeSS contract comparison with optional features represented without semantic loss. |
| Optical flow as general MV fallback | Visual and temporal validation including animated geometry, particles, disocclusion, and VR head motion. |
| Shared monitor/VR pipeline | Independent synthetic per-eye transport/history is now host-tested; an OpenXR graphics session, frame pacing, presentation and physical-headset binocular validation are still required. |

## Next validation gate: one real vertical slice

The next experiment should connect the already-proven pieces through one real D3D9Ex title before adding another source API or reconstruction backend:

```text
real D3D9Ex renderer
        |
        v
per-device / per-swapchain observation
        |
        v
TemporalFrame semantics
        |
        v
x86 -> x64 GPU transport
        |
        v
XeSS Native AA 1:1
        |
        v
game output
```

Success means the same frame identity, resource generation, reset state, color/depth/MV semantics and history validity can be followed through the complete route. The real adapter must fail open: loss or rejection of reconstruction may disable the experiment, but it must not deliberately fail the game's renderer.

Keep validation and runtime responsibilities separate. CPU readback, pixel-by-pixel verification and synchronous callback logging remain diagnostic tools. Any synchronization retained in the frame path must be justified by measurements from the real integration rather than inherited from a probe.

Use XeSS Native AA first because its backend contract already executes locally at 1:1. Backend comparison, SR, D3D10/D3D8 expansion and OpenXR session work resume after this vertical gate exposes which abstractions are actually reusable.

## Architectural risks

1. **Temporal semantics dominate transport.** A technically perfect x86/x64 bridge is not useful if legacy inputs are wrong.
2. **Motion vectors may require engine-specific knowledge.** Camera-only reconstruction cannot describe skinned meshes, particles, independent weapons, or animation.
3. **D3D9 sharing has a narrow documented interop path.** On the current host, classic D3D9 shared-texture creation fails for every tested case while D3D9Ex successfully reaches D3D11 and the existing x86/x64 bridge through a constrained relay. This remains host/driver-scoped evidence, so other systems still require capability detection rather than assuming the same boundary.
4. **D3D10 sits between generations.** It can share legacy DXGI resources but lacks D3D11.1 NT handles, D3D11-style fences, and UAV capability needed by modern output paths.
5. **Super Resolution changes the renderer.** Real SR requires control over internal render resolution, projection jitter, resource extents, post-processing, and UI composition; post-process downscale/re-upscale is not equivalent.
6. **VR amplifies latency and temporal mistakes.** Per-eye divergence and head-motion errors can be uncomfortable even if screenshots look sharp.
7. **Probe lifecycle assumptions are intentionally weak.** Current controlled hooks can rely on global state, cooperative loading and simple device lifetimes. A real adapter needs per-device/per-swapchain ownership, reset-safe resource generations, bounded teardown and fail-open behavior before it can be treated as runtime architecture.
