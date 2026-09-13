# LTR Bridge — Legacy Temporal Reconstruction Bridge

LTR Bridge is an independent research project exploring whether modern temporal reconstruction and antialiasing techniques can be applied to legacy renderers, including eventual use in VR mods.

The primary research targets are DLSS Super Resolution, DLAA, FSR-family temporal upscalers, and XeSS. The source renderers of interest begin with Direct3D 8, 9, 10, and 11, with both x86 and x64 applications in scope.

This repository is not part of any existing VR mod. Existing mods may become future consumers or validation cases, but they are not dependencies and must not be modified from this project.

## Current conclusion

The first research pass supports a **candidate** layered design:

`legacy renderer -> API adapter -> temporal data provider -> optional transport -> modern host -> reconstruction backend -> game/VR output`

That shape is plausible but not yet validated as a universal architecture. The strongest reusable pattern observed so far is the cross-bitness GPU-resource bridge used by DLSS5-Feeder: a 32-bit client can keep frame data on the GPU while a 64-bit helper executes modern work, provided the source API can reach shareable resources and synchronization primitives either directly or through an intermediate API device.

The largest unresolved problem is not calling an upscaler. It is producing trustworthy temporal inputs from engines that were never designed for them, especially dense motion vectors, correct depth, projection jitter, history resets, and independent per-eye state for VR.

## Initial prototype direction

The current roadmap favors a controlled D3D11 x64 harness first, with DLAA/native-resolution temporal AA as the initial backend experiment. This avoids internal-resolution changes while validating color, depth, motion vectors, jitter, history, and reset behavior. A D3D11 x86 transport probe follows before D3D10 and D3D9 experiments.

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
- [docs/REFERENCES.md](docs/REFERENCES.md) — research snapshot and source versions.

## Validation vocabulary

Repository claims use explicit maturity states: `planned`, `implemented`, `host-tested`, `live-tested`, `visual-validated`, `performance-validated`, `vr-headset-validated`, and `supported`.

As of 2026-09-13 this repository contains research and design documents only. No production injector or reconstruction runtime is implemented.

## License

LTR Bridge's original code and documentation are released under the [MIT License](LICENSE). Third-party SDKs, runtimes, tools, and redistributed components retain their own licenses and distribution terms; see [docs/REFERENCES.md](docs/REFERENCES.md).