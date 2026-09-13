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

Research and repository bootstrap only. Minimal probes are allowed when needed to resolve a specific uncertainty. Do not build a production injector yet.
