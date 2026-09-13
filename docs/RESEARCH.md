# Research snapshot — 2026-09-13

This document records the first research pass. It distinguishes upstream behavior from conclusions that still need local experiments.

## Evidence vocabulary

- **verified** — supported by primary documentation or directly inspectable upstream implementation/release evidence.
- **observed** — demonstrated by an upstream project, but not yet reproduced in this repository.
- **hypothesis** — technically plausible interpretation requiring a probe.
- **experiment-pending** — a concrete test has been identified but not run.

## DLSS5-Feeder

Research target: `jlrouzies-fr/DLSS5-Feeder`, current stable release observed as `v0.15.1`, short commit `3f62485`, released 2026-09-09. The releases page also shows a newer `1.16.0-beta.1`; stable conclusions below are based on the documented 0.15.x architecture unless stated otherwise.

### Cross-bitness path

**Observed upstream:** a 32-bit D3D11 game does not load NGX itself. The x86 add-on creates or opens GPU-shareable resources, starts `dlss5-feed-host64.exe`, and exchanges control data/handles through a named pipe. The x64 host owns a D3D12 device, opens the shared resources, runs the modern evaluation, then signals completion back.

**Verified platform mechanism:** D3D11/D3D12 interop can use NT shared handles for resources and shared fences. Microsoft documents `ID3D11Device5::OpenSharedFence`, `ID3D11Device::OpenSharedResource1`, and D3D12 `CreateSharedHandle`/`OpenSharedHandle` for this interop.

**Observed upstream:** frame pixels do not need a CPU round-trip. The Feeder design keeps frame payloads in GPU resources; CPU IPC carries coordination and handles, while GPU copies may still occur around the shared resources. This is resource sharing, not automatically zero-copy.

### Resource ownership direction

**Observed upstream:** D3D11 clients can normally create NT-handle shared textures that the D3D12 host opens. Where a D3D11 feature-level/output combination cannot create the needed shared UAV-capable output, the host can create the resource set and hand handles back to the game.

**Observed upstream:** OpenGL/Vulkan paths have reasons to reverse creation direction so the D3D12 host creates shareable allocations that the client imports.

**Implication:** transport should permit resource ownership to be negotiated per source API/capability rather than hard-coded game-side or host-side.

### D3D10

**Observed upstream:** current Feeder has a native 32-bit D3D10 path without dgVoodoo2. It creates a private D3D11 relay device in the game process on the same adapter. D3D10 copies color/depth/MV into legacy shared textures; D3D11 opens them and then uses the existing D3D11-to-host path.

The upstream explanation identifies three direct D3D10 limitations for its helper design:

1. no D3D11.1 NT-handle resource sharing path directly from the D3D10 device to D3D12;
2. no D3D11-style shared fence interface;
3. no UAV support for the output shape expected by the modern path.

**Verified platform nuance:** Microsoft documents legacy D3D10/DXGI shared resources and keyed-mutex synchronization for D3D10.1. Feeder reports that keyed-mutex creation returned `E_INVALIDARG` on tested modern hardware and therefore used event queries. This repository must treat that as an implementation/hardware observation, not as a universal invalidation of the documented API.

### DLAA vs real Super Resolution

**Observed upstream:** Feeder intentionally exposes a 1:1 DLAA-style temporal contract. Its `work_resolution` option is documented as a cost knob that downscales, processes, and re-expands; it is explicitly not real DLSS Quality/Balanced/Performance because the game renderer is not being driven at a lower internal resolution with proper jitter and temporal inputs.

**Conclusion:** real Super Resolution requires control earlier in the renderer than a finished backbuffer post-process. LTR Bridge must not label post-process resize paths as DLSS SR.

## DLSS5-Swapper

**Observed upstream:** Swapper is best treated as ecosystem/deployment evidence. Current releases automate detection, backup/restore, Feeder/OptiScaler routes, dgVoodoo2 deployment, x86/x64 distinctions, configuration, and third-party component handling.

It demonstrates that a practical user-facing tool needs API detection, component version pinning, conflict handling, and reversible installation. It does not establish the internal architecture LTR Bridge should use.

Current release notes observed around 2026-09 list Feeder 0.15.1, ReShade 6.8.0 and dgVoodoo2 2.87.4 as deployed components.

## OptiScaler

Research target: `optiscaler/OptiScaler`, latest stable release observed as `v0.9.4`, short commit `7534ad0`.

**Verified/observed:** current OptiScaler supports D3D11, D3D12 and Vulkan paths and can bridge among existing DLSS2+/FSR2+/XeSS-style integrations. Some D3D11 backends use D3D11-on-12 or a background D3D12 device, with documented overhead.

**Conclusion:** OptiScaler is highly relevant once a game already exposes a modern temporal-upscaler contract. It is not, by itself, the missing legacy temporal-data extraction layer for a D3D8/9 game with no depth/MV/jitter integration.

**Possible reuse:** backend substitution patterns, D3D11/D3D12 interop lessons, configuration, compatibility handling, and coexistence behavior.

## ReShade

Research target: ReShade 6.8.0 ecosystem, with current `crosire/reshade` main observed through 2026-09-10 (`a33de92` shown in commit history).

**Verified from source:** the add-on API has device implementations for D3D9, D3D10, D3D11, D3D12, OpenGL and Vulkan. The generic depth add-on contains API-specific handling; for D3D9 it may substitute sampleable depth formats, while D3D10/11 depth textures can be made typeless/sampleable when creation can be intercepted.

**Observed upstream:** D3D10 ReShade effects compile at Shader Model 4, constraining MV providers that require compute shaders. Feeder recommends providers with non-compute/pixel-shader paths for this reason.

**Conclusion:** ReShade is a strong prototyping substrate because it already solves broad injection/API observation and depth discovery. It should not become an obligatory production dependency until direct adapters are compared for visibility, coexistence, performance, and VR integration.

## dgVoodoo2

**Observed ecosystem use:** current tools deploy dgVoodoo2 as a D3D8/D3D9 -> D3D11 translation route before ReShade/modern processing.

Potential benefit: downstream logic sees a D3D11 device and can reuse modern sharing/resource code.

Potential cost: the translation layer can hide or transform original D3D9 state that a direct adapter might use to recover per-draw transforms or game-specific temporal information. Proxy-DLL ownership and compatibility with other injectors/mods also become part of the stack.

**Experiment-pending:** compare direct D3D9 interception against dgVoodoo2 on the same target. No default route is selected yet.

**Verified from the current primary distribution terms:** dgVoodoo2 permits selected files to accompany a specific game/game mod, while general standalone redistribution has stricter packaging requirements and embedding it in a general-purpose launcher/framework is restricted. Therefore LTR Bridge must not assume it can redistribute dgVoodoo2 as a generic built-in translation component even if the technical experiment favors that route.

## NVIDIA DLSS / Streamline temporal contract

**Verified from NVIDIA Streamline documentation:** DLSS Super Resolution requires render-resolution input color, output color, depth, and motion vectors. Common constants include jitter, motion-vector scale, camera transformations/depth conventions, frame identity, and reset state. Exposure can be supplied explicitly or an auto-exposure path can be used.

NVIDIA explicitly requires correct jitter and motion-vector scaling; matrices supplied to Streamline should not contain the jitter that is instead passed separately.

**Conclusion:** a legacy integration that only has final color plus optical flow is not equivalent to a renderer-native DLSS integration. It may still be useful experimentally for native-resolution temporal processing, but should be labeled by the actual fidelity of its inputs.

## FidelityFX temporal upscaling

**Verified from current FidelityFX SDK documentation:** temporal upscaling consumes motion vectors, depth, jittered render-resolution inputs, and optional exposure/reactive/transparency-composition information. Motion-vector resolution/conventions are configurable. Reactive information helps where shading changes are not represented by depth or motion vectors, such as alpha-blended particles.

**Implication:** a minimal cross-backend contract needs an extension mechanism for masks and backend-specific semantic flags. Treating every backend as only `{color, depth, mv}` would lose useful information.

## Intel XeSS

Research target: current Intel XeSS SDK repository, which identifies itself as XeSS 3; the XeSS-SR Developer Guide 2.0 documents both SR and Native Anti-Aliasing modes.

**Verified:** XeSS-SR requires jitter and color plus either high-resolution dilated MVs, or low-resolution MVs with depth. It documents current-to-previous screen-space vectors, pixel/NDC scaling, no jitter in the MV values, inverted-depth support, optional responsive-pixel masks, and Native Anti-Aliasing at 1.0x.

**Implication:** XeSS Native AA is another useful controlled 1:1 temporal experiment and confirms that native-resolution validation is not uniquely a DLAA concept.

## First-pass conclusions

### Verified/strongly supported

- Modern temporal reconstruction depends on correct temporal semantics, not merely access to the final backbuffer.
- Real SR needs control of render resolution and projection jitter; post-process resize is not equivalent.
- D3D11/D3D12 can support cross-process GPU resource and fence sharing through NT handles.
- x86 game -> x64 modern helper is technically viable; DLSS5-Feeder demonstrates this pattern in real software.
- D3D10 requires a different bridge strategy than D3D11 for modern helper interop; a private D3D11 relay is a demonstrated option.
- ReShade offers useful API/depth observation across D3D9/10/11 but has API-specific constraints.
- OptiScaler assumes a substantially more modern temporal integration than many legacy games provide.
- DLSS, FidelityFX temporal upscaling, and XeSS overlap on color/depth/MV/jitter concepts but do not have identical contracts.

### Hypotheses requiring local experiments

- D3D11 x64 + native-resolution AA is the best first harness for validating the contract.
- A generic x86/x64 transport can be backend-independent if it transports typed frame resources and synchronization rather than DLSS-specific state.
- D3D10 can reuse most of a D3D11 transport behind a relay without unacceptable latency.
- Direct D3D9 interception will preserve useful temporal reconstruction opportunities that may be harder to recover after translation.
- Per-eye temporal reconstruction can fit the same broad layers while requiring separate history, timing, and validation policies.

## Immediate research gaps

1. Complete the current NVIDIA binary/SDK redistribution review and re-check all third-party terms immediately before any shipping decision; dgVoodoo2's primary distribution terms are now recorded above.
2. Exact API/version limits for each reconstruction backend on D3D11 vs D3D12 and x86 vs x64.
3. Controlled measurement of copies and latency in a D3D11 x86 -> D3D12 x64 bridge.
4. Motion-vector quality ladder on static geometry, skinned geometry, particles, and independently moving first-person/VR objects.
5. A renderer-resolution control strategy for real SR in engines that hard-code backbuffer-sized targets.
