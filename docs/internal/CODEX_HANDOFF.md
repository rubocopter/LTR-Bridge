# Codex handoff

Updated: 2026-09-13.

## Repository state

This workspace started empty. The first pass created research/design documentation only; there is no production code or injector.

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

## Candidate architecture

`Legacy API Adapter -> Temporal Data Provider -> optional Transport -> Modern Graphics Host -> Reconstruction Backend -> game/VR output`

Treat this as a hypothesis until the first probes are complete.

## Next concrete work

1. Re-inspect repository state and this handoff.
2. Finish the current NVIDIA redistribution review. dgVoodoo2 primary distribution terms were reviewed in this pass and constrain generic framework bundling; see `docs/REFERENCES.md` and `docs/RESEARCH.md`.
3. Build a controlled D3D11 x64 temporal harness. Prefer native-resolution temporal AA first so color/depth/MV/jitter/history can be validated without internal-resolution changes.
4. Add diagnostic visualizations for depth, motion direction/scale and history reset.
5. Build a backend-neutral D3D11 x86 -> D3D12 x64 round-trip resource-sharing probe.
6. Only after those are proven, reproduce the D3D10 relay and compare D3D9 native vs dgVoodoo2 paths.

## Important upstream snapshot

- DLSS5-Feeder stable observed: `v0.15.1` / `3f62485`.
- OptiScaler stable observed: `v0.9.4` / `7534ad0`.
- ReShade ecosystem version observed: 6.8.0; GitHub commit history seen through 2026-09-10 (`a33de92` shown for that date).

## Environment limitation encountered

The outer environment blocked direct `git clone` of research repositories and also blocked `git init` through the command tool. Research therefore used current public repository/documentation views, while repository files were created through the native patch mechanism. Re-check Git initialization status before assuming this workspace is already a Git repository.

## Do not do next

Do not begin a production injector, do not modify existing game-mod repositories, and do not freeze a public universal temporal ABI before at least DLSS/DLAA, FidelityFX and XeSS mappings plus one x86 transport and one legacy API experiment have informed it.
