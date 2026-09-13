# D3D11 x64 temporal harness

Status: **implemented, host-tested and visual-validated for the current static/camera/rigid-object ground-truth and camera+depth cases; performance validation pending**.

The Phase 1 harness is a controlled D3D11 x64 research renderer. It separates renderer projection jitter from ground-truth camera/rigid-object motion, exposes scene/depth/motion diagnostics, and now includes a camera+depth reconstruction baseline that can be compared directly against renderer ground truth.

The current motion convention is current-pixel -> previous-pixel in render-pixel units. Projection jitter is excluded from the motion vectors and carried separately. Coverage currently includes camera and rigid-object motion; skinned/cloth geometry, particles, transparency, and HUD are explicit known exclusions.

Build from the repository root with:

```powershell
cmake -S . -B build -A x64
cmake --build build --config Release
```

The September 13, 2026 host check built successfully with MSVC 19.44 / Windows SDK 10.0.26100.0. The harness now also exposes `--self-test`, registered through CTest. It renders deterministic reset/steady frames for static, camera-translation, camera-rotation and rigid-object scenarios, performs GPU -> staging readback of depth and motion, verifies frame/history metadata and motion expectations, and writes a text report plus depth/MV diagnostic BMPs. The same test is run by the Windows GitHub Actions workflow.

During this validation the readback test exposed a real harness defect: the per-draw constant buffer was bound to the vertex shader but not the pixel shader, so `renderSize` read as zero in `ScenePS` and all motion vectors collapsed to zero. The fix binds `PerDraw` to both VS and PS. A second failure exposed that the intended static reference triangle was back-face culled; its winding was corrected rather than weakening the rigid-object coverage criterion.

The final deterministic run passes all four scenarios. Visual inspection of the generated diagnostics confirms: static MV is neutral; camera translation and rotation produce continuous depth-dependent fields; and rigid-object motion is isolated from the static reference geometry. This is **visual-validated** evidence for the current controlled cases only, not for excluded content or reconstruction quality.

The camera+depth baseline reconstructs the current world position from hardware depth with the inverse jittered current view-projection matrix, then reprojects it through current/previous unjittered camera matrices. The deterministic comparison is **host-tested**: camera translation measures 0.000404 px mean / 0.001781 px max error, and camera rotation 0.000811 px mean / 0.004889 px max error. In the rigid-object scenario the camera remains static, so the baseline intentionally reconstructs zero object motion while renderer ground truth marks 20,686 moving pixels; all 20,686 are reported as `camera_only_miss`.

The reconstructed-motion and reconstruction-error BMPs were also inspected. Camera-motion fields are continuous, static error is effectively zero, and the rigid-object error map isolates the moving object while the static floor remains at zero. This is **visual-validated** evidence for this controlled camera-only provider and its known rigid-object limitation; it is not evidence of object-motion coverage in camera+depth reconstruction.

Controls are `1` static scene, `2` camera translation, `3` camera rotation, `4` rigid-object motion, `Tab` diagnostic view (scene, depth, renderer motion, reconstructed motion), `R` history reset, and `Esc` exit.

Performance, skinned/cloth/particle/transparency/HUD cases, optical flow, reconstruction backends, SR, and stereo validation remain pending.
