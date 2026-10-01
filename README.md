# LTR Bridge — Legacy Temporal Reconstruction Bridge

<p align="center">
  <a href="LICENSE"><img alt="License: MIT" src="https://img.shields.io/badge/license-MIT-blue?style=flat-square"></a>
  <img alt="Status: research" src="https://img.shields.io/badge/status-research-blueviolet?style=flat-square">
</p>
<p align="center">
  <a href="https://ko-fi.com/onitaku"><img alt="Support me on Ko-fi" src="https://ko-fi.com/img/githubbutton_sm.svg"></a>
</p>

**Research into bringing modern temporal reconstruction and antialiasing to legacy renderers — with VR kept in scope from the start.**

LTR Bridge explores how older Direct3D 8/9/10/11 games could eventually feed modern techniques such as DLSS, DLAA, XeSS and FSR-family temporal reconstruction without forcing every game or renderer into one architecture.

> **Research project — no production release.** The transport side is increasingly well understood; the harder open problem is producing trustworthy depth, motion vectors, jitter and history semantics from engines that were never designed for temporal reconstruction.

## What has been demonstrated

Current evidence as of **2026-10-01**, scoped to the tested builds and host:

| Area | Evidence and limit |
| --- | --- |
| Temporal inputs | **Host-tested / visual-validated** in a controlled D3D11 harness: readable depth, camera/projection jitter, ground-truth motion vectors and explicit history reset. Real-game temporal provenance remains open. |
| GPU transport | **Host-tested** x86 D3D11 → x64 D3D12 synchronization, resource replacement, backpressure, controlled failures and synthetic per-eye history isolation. D3D9Ex and D3D10.1 relays are host-tested separately. |
| Clean Steam Call of Juarez | **Live-tested** bounded D3D9Ex compatibility and two-slot color transport across an engine reset: the stalled old generation is cancelled and the replacement completes 12/12 frames with zero sampled mismatches. Validation checks one pixel per frame, not the full image. |
| Reconstruction and XR | **Host-tested** XeSS **3.0.2 Native AA 1:1** in a controlled D3D12 probe, plus an isolated OpenXR runtime bootstrap. Neither establishes reconstructed game output or headset presentation. |

The observed Call of Juarez MANAGED-resource startup failures are resolved for
the exercised resource profiles. Broad D3D9Ex compatibility remains unproven.

## What remains open

The immediate **experiment-pending** gate is coherent real-game color, readable
depth, effective camera, jitter and motion vectors, with identity preserved across
skipped frames, resource generations and views/eyes. Then feed XeSS Native AA at
1:1 and return its result to the game.

Sustained transport, broader reset/device-loss recovery, full-image correctness,
end-to-end reconstruction quality, latency and headset validation remain open.
The bounded reset result above does not close those gates.

## Project boundaries

LTR Bridge is independent of [Call of Juarez VR](https://github.com/rubocopter/call_of_juarez_vr).
The mod's accepted OpenVR stereo, normal shutdown and configured-HMD rate cap
are separate evidence; they do not validate LTR's D3D12 reconstruction or OpenXR
path. This project does not add dependencies to existing mod repositories.
The mod's controller-only menus and body comfort remain unresolved product
gates; its native mouse-event and body-yaw corrections are host-tested pending
another headset run. They do not advance LTR's temporal-input gate.

## Start with the docs

[Roadmap](ROADMAP.md) · [Architecture](ARCHITECTURE.md) · [Research](docs/RESEARCH.md) · [Compatibility](docs/COMPATIBILITY.md) · [Temporal contract](docs/TEMPORAL_CONTRACT.md) · [VR notes](docs/VR.md)

More focused material is available under `docs/` for D3D8/9/10, depth, motion vectors, jitter, the x86/x64 bridge, XeSS, OpenXR and case studies.

<details>
<summary><strong>Research and validation model</strong></summary>

Claims in this repository deliberately distinguish `implemented`, `host-tested`, `live-tested`, `visual-validated`, `performance-validated`, `vr-headset-validated` and `supported` results. A successful probe on one API, driver, GPU or game is not treated as universal support.

See [AGENTS.md](AGENTS.md) for the working rules and [docs/internal/CODEX_HANDOFF.md](docs/internal/CODEX_HANDOFF.md) for the current engineering checkpoint.

</details>

## License

Original code and documentation are released under the [MIT License](LICENSE). Third-party components retain their own terms; see [docs/REFERENCES.md](docs/REFERENCES.md).
