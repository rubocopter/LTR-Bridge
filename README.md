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

- Host-tested **x86 D3D11 → x64 D3D12** GPU transport with synchronization, resource replacement and stereo isolation.
- Host-tested legacy relay paths from **D3D9Ex** and **D3D10.1** into the modern transport.
- A real Call of Juarez D3D9 compatibility path has carried validated game-frame data into the x64 host.
- An optional **XeSS 3.0.2 Native AA** probe and an isolated **OpenXR** runtime bootstrap are working in controlled tests.

## What remains open

- Reliable temporal inputs from real legacy engines, especially motion vectors and depth semantics.
- Sustained multiframe real-game integration and reset/resource-generation handling.
- Reconstruction output integrated back into a game or VR presentation path.
- Headset and performance validation.

## Start with the docs

[Roadmap](ROADMAP.md) · [Architecture](ARCHITECTURE.md) · [Research](docs/RESEARCH.md) · [Compatibility](docs/COMPATIBILITY.md) · [Temporal contract](docs/TEMPORAL_CONTRACT.md) · [VR notes](docs/VR.md)

More focused material is available under `docs/` for D3D8/9/10, depth, motion vectors, jitter, the x86/x64 bridge, XeSS, OpenXR and case studies.

<details>
<summary><strong>Research and validation model</strong></summary>

Claims in this repository deliberately distinguish `implemented`, `host-tested`, `live-tested`, `visual-validated`, `performance-validated`, `vr-headset-validated` and `supported` results. A successful probe on one API, driver, GPU or game is not treated as universal support.

See [AGENTS.md](AGENTS.md) for the working rules and [docs/internal/CODEX_HANDOFF.md](docs/internal/CODEX_HANDOFF.md) for the current engineering checkpoint.

</details>

## License and support

Original code and documentation are released under the [MIT License](LICENSE). Third-party components retain their own terms; see [docs/REFERENCES.md](docs/REFERENCES.md).

If you want to support continued research, see [Ko-fi](https://ko-fi.com/onitaku).
