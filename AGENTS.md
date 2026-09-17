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

Research and vertical-integration experiments only. Clean Steam Call of Juarez is now a host-tested real-game D3D9 target. Its legacy `Direct3DCreate9` entry can be experimentally backed by `Direct3DCreate9Ex` without changing the game's API surface; the resulting device reaches `Present`, exposes color/depth/transforms, and carries the real `2560x1440` frame through a D3D9Ex relay, private x86 D3D11 and a one-shot NT-shared resource into an x64 D3D12 process with matching validation samples. The exact two-slot x86/D3D11 -> x64/D3D12 transport contract intended to replace that one-shot handoff is host-tested offline with x64-owned shared resources, separate ready/done fences, forced backpressure and zero content mismatches, and its shared client is already connected to the real observer in compile-tested source. The immediate gate is live-testing that connection in the game, including reset/resource-generation and fail-open lifecycle, then defining the real temporal inputs and exercising one reconstruction backend. Minimal probes and narrowly scoped integration code are allowed when needed to resolve a specific uncertainty. Do not build a production injector yet.
