# Research snapshot — 2026-09-13

This document records the first two research passes. It distinguishes upstream behavior from conclusions that still need local experiments.

## Evidence vocabulary

- **verified** — supported by primary documentation or directly inspectable upstream implementation/release evidence.
- **observed** — demonstrated by an upstream project, but not yet reproduced in this repository.
- **hypothesis** — technically plausible interpretation requiring a probe.
- **experiment-pending** — a concrete test has been identified but not run.

## DLSS5-Feeder

Research target: `jlrouzies-fr/DLSS5-Feeder`, current stable release observed as `v0.15.1`, short commit `3f62485`, released 2026-09-09. The newest prerelease observed is `v1.16.0-beta.1`, short commit `55c5bca`, released 2026-09-10. Stable conclusions below remain based on the documented 0.15.x architecture unless stated otherwise.

### Cross-bitness path

**Observed upstream:** a 32-bit D3D11 game does not load NGX itself. The x86 add-on creates or opens GPU-shareable resources, starts `dlss5-feed-host64.exe`, and exchanges control data/handles through a named pipe. The x64 host owns a D3D12 device, opens the shared resources, runs the modern evaluation, then signals completion back.

**Verified platform mechanism:** D3D11/D3D12 interop can use NT shared handles for resources and shared fences. Microsoft documents `ID3D11Device5::OpenSharedFence`, `ID3D11Device::OpenSharedResource1`, and D3D12 `CreateSharedHandle`/`OpenSharedHandle` for this interop.

**Observed upstream:** frame pixels do not need a CPU round-trip. The Feeder design keeps frame payloads in GPU resources; CPU IPC carries coordination and handles, while GPU copies may still occur around the shared resources. This is resource sharing, not automatically zero-copy.

**Observed upstream, current release line:** the x86 add-on and x64 helper use IPC protocol v9 and must be deployed as a matching pair. The newer beta also adds more host/build-generation diagnostics and D3D11 synchronization fallbacks. This strengthens the requirement that LTR Bridge version its control protocol and make host/client build identity observable from the first bridge prototype.

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

**Verified licensing:** the current ReShade repository uses BSD-3-Clause as its project license. Relevant public API headers such as `reshade.hpp` and `reshade_api.hpp` explicitly permit BSD-3-Clause OR MIT. Any future reuse must still check the exact source file being copied rather than treating every third-party file in the repository as having identical terms.

## dgVoodoo2

**Observed ecosystem use:** current tools deploy dgVoodoo2 as a D3D8/D3D9 -> D3D11 translation route before ReShade/modern processing.

Potential benefit: downstream logic sees a D3D11 device and can reuse modern sharing/resource code.

Potential cost: the translation layer can hide or transform original D3D9 state that a direct adapter might use to recover per-draw transforms or game-specific temporal information. Proxy-DLL ownership and compatibility with other injectors/mods also become part of the stack.

**Experiment-pending:** compare direct D3D9 interception against dgVoodoo2 on the same target. No default route is selected yet.

**Verified from the current primary distribution terms:** dgVoodoo2 permits selected files to accompany a specific game/game mod, while general standalone redistribution has stricter packaging requirements and embedding it in a general-purpose launcher/framework is restricted. Therefore LTR Bridge must not assume it can redistribute dgVoodoo2 as a generic built-in translation component even if the technical experiment favors that route.

## D3D9 -> D3D11 interop: second-pass finding

**Verified from Microsoft documentation:** `ID3D11Device::OpenSharedResource` explicitly documents opening a Direct3D 9 texture in D3D11 when the D3D9 texture was created with `CreateTexture(..., pSharedHandle)`. The documented path is deliberately narrow: 2D, one mip, default usage, no MSAA and only a small set of shareable color formats.

**Verified documentation conflict/qualification:** Microsoft's broader cross-API surface-sharing overview says unsynchronized surface sharing is supported by D3D9Ex and that D3D9c/older runtimes do not support shared surfaces. The two primary pages therefore cannot safely be collapsed into the claim that every D3D9 game can share directly with D3D11.

**Hypothesis:** if the route works for the target runtime, a native D3D9 adapter could preserve original renderer visibility while copying/encoding temporal payloads into purpose-built shared textures, open those textures in a private D3D11 relay, and reuse the D3D11 -> x64 modern-host transport. This could avoid whole-renderer translation through dgVoodoo2.

**Experiment-pending:** a minimal D3D9/D3D9Ex producer -> D3D11 consumer probe must test classic D3D9 and D3D9Ex separately, the documented formats, pixel correctness, synchronization, resize/recreation and driver/OS behavior before this route is used architecturally.

## NVIDIA DLSS / Streamline temporal contract

**Verified from NVIDIA Streamline documentation:** DLSS Super Resolution requires render-resolution input color, output color, depth, and motion vectors. Common constants include jitter, motion-vector scale, camera transformations/depth conventions, frame identity, and reset state. Exposure can be supplied explicitly or an auto-exposure path can be used.

NVIDIA explicitly requires correct jitter and motion-vector scaling; matrices supplied to Streamline should not contain the jitter that is instead passed separately.

**Conclusion:** a legacy integration that only has final color plus optical flow is not equivalent to a renderer-native DLSS integration. It may still be useful experimentally for native-resolution temporal processing, but should be labeled by the actual fidelity of its inputs.

### Streamline bitness and NVIDIA distribution terms

**Verified:** the current Streamline programming guide specifies Windows 10 RS3 64-bit or newer for SL features, and the current build defaults to the `amd64` target. This makes an x64 execution environment a concrete requirement for the primary NVIDIA integration path rather than merely an implementation choice copied from Feeder.

**Verified from the NVIDIA RTX SDK license, v. March 14 2024:** covered SDK software/materials may be distributed when incorporated in object-code form into an application subject to the license requirements; standalone SDK redistribution is prohibited. The application must add material functionality, and the DLSS/NGX supplement includes NVIDIA-GPU use, attribution and pre-commercial-release notification requirements.

**Architectural implication:** LTR Bridge should keep proprietary backend binaries outside its generic transport contract and treat packaging as a backend/deployment concern. Before release, the then-current license and the exact binaries selected must be re-reviewed.

## FidelityFX temporal upscaling

**Verified from current FidelityFX SDK documentation:** temporal upscaling consumes motion vectors, depth, jittered render-resolution inputs, and optional exposure/reactive/transparency-composition information. Motion-vector resolution/conventions are configurable. Reactive information helps where shading changes are not represented by depth or motion vectors, such as alpha-blended particles.

**Implication:** a minimal cross-backend contract needs an extension mechanism for masks and backend-specific semantic flags. Treating every backend as only `{color, depth, mv}` would lose useful information.

**Verified current SDK state:** the current repository identifies itself as AMD FSR SDK 2.3.0. Its FSR API exposes a small five-function ABI through provided signed DLLs, and current documentation says backend-specific functionality is supported through the DirectX 12 DLL. The SDK repository also discusses Vulkan at a broader level, so the exact deployment path must be scoped to the API being used rather than inferred from the repository headline.

**Verified licensing/distribution:** AMD's current SDK license permits use and redistribution of the covered software in binary form subject to retaining the required notices and terms, and prohibits reverse engineering/decompilation/disassembly of those binaries. The SDK license separately enumerates source files/components with their own terms, so LTR Bridge should prefer the documented signed-binary API boundary unless and until a specific source component is selected and its exact license is reviewed.

**Artifact inspection:** the current `Kits/FidelityFX/signedbin` directory exposes one DX12 loader/upscaler DLL set without architecture-specific filenames. This does not establish the PE machine type. Current x86 binary availability therefore remains unverified; do not assume either x86 support or x64 exclusivity until the actual binary headers can be inspected. D3D12 remains the strongest first modern-host target regardless.

## Intel XeSS

Research target: Intel XeSS SDK `v3.0.2`, short commit `8fe81bd`, released 2026-07-24. The XeSS-SR Developer Guide 2.0 documents both SR and Native Anti-Aliasing modes.

**Verified:** XeSS-SR requires jitter and color plus either high-resolution dilated MVs, or low-resolution MVs with depth. It documents current-to-previous screen-space vectors, pixel/NDC scaling, no jitter in the MV values, inverted-depth support, optional responsive-pixel masks, and Native Anti-Aliasing at 1.0x.

**Implication:** XeSS Native AA is another useful controlled 1:1 temporal experiment and confirms that native-resolution validation is not uniquely a DLAA concept.

**Verified execution constraints:** XeSS-SR requires Windows 10/11 x64. Its D3D12 path is documented for Intel and other vendors meeting the feature requirements, while the D3D11 SR path is currently limited to Intel Arc or later. For a generic backend, this favors an x64 D3D12 host over treating D3D11 as the universal XeSS execution API.

**Observed current SDK direction:** XeSS 3.0.0 added external-memory-heap support for sharing GPU memory with other engine components, and the current D3D12 SR path permits application-provided temporary heaps/descriptors. This is useful evidence that modern backends can cooperate with externally managed memory, but it is not proof that XeSS supplies LTR Bridge's cross-process transport.

**Verified licensing:** the current XeSS SDK is distributed under Intel Simplified Software License (Version October 2022). Intel permits use and redistribution of the binary software without modification when the copyright/license terms are reproduced, prohibits using Intel/supplier names for endorsement without permission, and prohibits reverse engineering, decompilation, disassembly or modification of the binary. Third-party components retain separate terms.

## OpenXR / VR timing contract

**Verified from OpenXR 1.1:** `xrWaitFrame` supplies a `predictedDisplayTime` for the application-generated frame, and the same target display time should be propagated through the rendering pipeline. `xrLocateViews` returns the predicted `XrView` data for that display time, one view per configured view. Recommended image extents are exposed per view.

**Implication:** VR temporal metadata should preserve view identity and predicted display time, and validation should log which predicted view matrices generated each eye's temporal inputs. Per-eye reconstruction should complete before those eye images are submitted in the projection layer. The exact placement relative to a legacy mod's own reprojection/pacing path remains experiment-pending.

## Second-pass architectural conclusion

The evidence now supports treating the x64 modern host as a reusable capability rather than an NVIDIA-specific workaround. Streamline targets 64-bit Windows, XeSS-SR is x64, and the current FSR API centers its backend-specific path on D3D12. The exact backend can remain replaceable while legacy adapters converge on a modern resource/synchronization boundary.

This does **not** justify making D3D12 mandatory forever. It makes D3D12 x64 the best-supported first host for the controlled bridge experiments because it maximizes overlap among current backend paths and proven sharing primitives.

## First-pass conclusions

### Verified/strongly supported

- Modern temporal reconstruction depends on correct temporal semantics, not merely access to the final backbuffer.
- Real SR needs control of render resolution and projection jitter; post-process resize is not equivalent.
- D3D11/D3D12 can support cross-process GPU resource and fence sharing through NT handles.
- x86 game -> x64 modern helper is technically viable; DLSS5-Feeder demonstrates this pattern in real software.
- D3D10 requires a different bridge strategy than D3D11 for modern helper interop; a private D3D11 relay is a demonstrated option.
- Microsoft documents a constrained D3D9 -> D3D11 shared-texture path, but classic D3D9 versus D3D9Ex applicability is unresolved and must be probed.
- ReShade offers useful API/depth observation across D3D9/10/11 but has API-specific constraints.
- OptiScaler assumes a substantially more modern temporal integration than many legacy games provide.
- DLSS, FidelityFX temporal upscaling, and XeSS overlap on color/depth/MV/jitter concepts but do not have identical contracts.
- Current Streamline and XeSS requirements independently justify researching an x64 host for x86 legacy games; current FSR API design further strengthens D3D12 as the first common host target.

### Hypotheses requiring local experiments

- D3D11 x64 + native-resolution AA is the best first harness for validating the contract.
- A generic x86/x64 transport can be backend-independent if it transports typed frame resources and synchronization rather than DLSS-specific state.
- D3D10 can reuse most of a D3D11 transport behind a relay without unacceptable latency.
- Direct D3D9 interception plus a constrained D3D11 relay may preserve useful temporal reconstruction opportunities while reusing the modern transport, if the D3D9/D3D9Ex sharing probe succeeds.
- Per-eye temporal reconstruction can fit the same broad layers while requiring separate history, timing, and validation policies.

## Immediate research gaps

1. Re-check the exact backend binaries, source files and third-party notices selected for any future distribution immediately before shipping; current NVIDIA, AMD, Intel, ReShade and dgVoodoo2 primary terms are now recorded at the level needed for architecture research.
2. Inspect the PE machine type of current AMD FSR signed DLLs when direct binary inspection is available; filenames/tree structure alone do not establish x86/x64 support.
3. Run the minimal classic-D3D9 vs D3D9Ex -> D3D11 shared-texture probe before committing to a D3D9 transport architecture.
4. Controlled measurement of copies and latency in a D3D11 x86 -> D3D12 x64 bridge.
5. Motion-vector quality ladder on static geometry, skinned geometry, particles, and independently moving first-person/VR objects.
6. A renderer-resolution control strategy for real SR in engines that hard-code backbuffer-sized targets.
