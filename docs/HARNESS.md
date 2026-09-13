# D3D11 x64 temporal harness

Status: **implemented, host-tested; visual/performance validation pending**.

The Phase 1 harness is a controlled D3D11 x64 research renderer. It separates renderer projection jitter from ground-truth camera/rigid-object motion and exposes scene, depth, and motion diagnostic views.

The current motion convention is current-pixel -> previous-pixel in render-pixel units. Projection jitter is excluded from the motion vectors and carried separately. Coverage currently includes camera and rigid-object motion; skinned/cloth geometry, particles, transparency, and HUD are explicit known exclusions.

Build from the repository root with:

```powershell
cmake -S . -B build -A x64
cmake --build build --config Release
```

The September 13, 2026 host check built successfully with MSVC 19.44 / Windows SDK 10.0.26100.0. A hidden smoke run remained alive and responsive for three seconds after D3D11 device/resource/shader initialization. This is **host-tested** evidence only; no image-quality claim follows from it.

Controls are `1` static scene, `2` camera translation, `3` camera rotation, `4` rigid-object motion, `Tab` diagnostic view, `R` history reset, and `Esc` exit.

Visual, performance, deterministic readback, skinned/cloth/particle/HUD cases, camera+depth reconstruction, optical flow, reconstruction backends, SR, and stereo validation remain pending.
