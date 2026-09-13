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
- `docs/D3D9.md`
- `docs/D3D10.md`
- `docs/MOTION_VECTORS.md`
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

## Candidate architecture

`Legacy API Adapter -> Temporal Data Provider -> optional Transport -> Modern Graphics Host -> Reconstruction Backend -> game/VR output`

Treat this as a hypothesis until the first probes are complete. The second pass strengthens D3D12 x64 as the first modern-host target, but does not make it a permanent universal requirement.

## Next concrete work

1. Re-inspect repository state and this handoff.
2. When direct binary inspection is available, inspect the PE machine type of the current AMD FSR signed loader/upscaler DLLs. Do not infer x86/x64 support from filenames. Primary license terms for NVIDIA, AMD, Intel, ReShade and dgVoodoo2 are already recorded; re-check exact selected components before shipping.
3. Before a large D3D9 experiment, build the smallest possible classic-D3D9/D3D9Ex -> D3D11 shared-texture probe to resolve the Microsoft-documentation ambiguity and measure synchronization/copy behavior.
4. Build a controlled D3D11 x64 temporal harness. Prefer native-resolution temporal AA first so color/depth/MV/jitter/history can be validated without internal-resolution changes.
5. Add diagnostic visualizations for depth, motion direction/scale and history reset.
6. Build a backend-neutral D3D11 x86 -> D3D12 x64 round-trip resource-sharing probe.
7. Reproduce the D3D10 relay and then compare D3D9 native dedicated transport, D3D9/D3D9Ex -> D3D11 relay, and dgVoodoo2 translation on the same target.

## Important upstream snapshot

- DLSS5-Feeder stable observed: `v0.15.1` / `3f62485`.
- DLSS5-Feeder newest prerelease observed: `v1.16.0-beta.1` / `55c5bca`.
- OptiScaler stable observed: `v0.9.4` / `7534ad0`.
- XeSS SDK current observed: `v3.0.2` / `8fe81bd`.
- AMD FSR SDK current observed: `2.3.0`.
- ReShade ecosystem version observed: 6.8.0; GitHub commit history previously seen through 2026-09-10 (`a33de92` shown for that date).

## Repository publication

- `origin` is `https://github.com/rubocopter/LTR-Bridge.git` on `main`.
- The initial bootstrap and this second research pass are published to GitHub.
- During the second research pass, shell Git commands were blocked by the execution environment. The workspace files were patched to mirror the documentation changes and GitHub was updated through the repository integration. The next agent must inspect/synchronize the local checkout before relying on local Git status.

## Do not do next

Do not begin a production injector, do not modify existing game-mod repositories, and do not freeze a public universal temporal ABI before at least DLSS/DLAA, FidelityFX and XeSS mappings plus one x86 transport and one legacy API experiment have informed it.
