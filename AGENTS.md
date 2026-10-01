# AGENTS.md

This repository is an independent research project for temporal reconstruction on legacy renderers. It must not modify, vendor, or introduce dependencies into `call_of_juarez_vr`, `penumbra_vr_framework`, or any other existing mod repository.

## Working rules

1. Inspect the current repository state before starting work. Do not assume roadmap items are still pending.
2. Prefer evidence and small experiments over framework construction.
3. Label conclusions as one of: **verified**, **observed**, **hypothesis**, **experiment-pending**, **implemented**, **host-tested**, **live-tested**, **visual-validated**, **performance-validated**, **vr-headset-validated**, or **supported**.
4. A result from one API, bitness, driver, GPU, or game is not universal evidence for another.
5. Do not promote compile success to runtime support.
6. Keep source-API adaptation, temporal-data production, transport, and reconstruction backends separable until experiments justify tighter coupling.
7. Do not make ReShade, dgVoodoo2, D3D12, NGX, optical flow, or any single motion-vector provider mandatory without evidence.
8. Preserve VR as an architectural requirement: per-eye inputs and histories must remain possible even if the first prototypes are flat-screen.
9. Record URLs and exact versions/tags/commits when a conclusion depends on an external implementation.
10. Keep third-party licensing and redistribution constraints visible in architectural decisions.
11. Maintain `docs/internal/CODEX_HANDOFF.md` whenever material conclusions, risks, experiments, or repository state change.
12. Prefer reproducible probes with explicit success criteria over game-specific hacks.

## Current phase

Research and vertical-integration experiments only. Clean Steam Call of Juarez is a live-tested real-game D3D9Ex target for bounded compatibility and color transport on the exercised build/host. Its legacy `Direct3DCreate9` entry is experimentally backed by `Direct3DCreate9Ex` without changing the game's API surface. The real observer's two-slot x86/D3D11 -> x64/D3D12 path has survived an engine reset: a stalled old generation was cancelled and the replacement completed 12/12 frames at `1920x1080`, with matching ready/done values, 12 consumer copies and zero sampled mismatches. Validation samples one pixel per frame; sustained operation, broader reset/device-loss recovery and full-image correctness remain experiment-pending. The immediate gate is coherent real-game temporal inputs and history/view identity, then XeSS 3.0.2 Native AA at 1:1 with output returned to the game. Independent CoJ VR OpenVR results do not validate LTR reconstruction or headset presentation. Minimal probes and narrowly scoped integration code are allowed when needed to resolve a specific uncertainty. Do not build a production injector yet.
