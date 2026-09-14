# D3D11 x64 temporal harness

Status: **implemented, host-tested and visual-validated for the current static/camera/rigid-object/deforming-geometry/disocclusion ground-truth, history-validity, camera+depth and synthetic optical-flow cases; performance validation pending**.

The Phase 1 harness is a controlled D3D11 x64 research renderer. It separates renderer projection jitter from ground-truth camera/rigid-object motion, exposes scene/depth/motion diagnostics, and now includes a camera+depth reconstruction baseline that can be compared directly against renderer ground truth.

The current motion convention is current-pixel -> previous-pixel in render-pixel units. Projection jitter is excluded from the motion vectors and carried separately. Coverage includes camera and rigid-object motion plus one controlled procedural per-vertex deformation case. Real engine skinned/cloth geometry, particles, transparency, and HUD remain explicit known exclusions.

Build from the repository root with:

```powershell
cmake -S . -B build -A x64
cmake --build build --config Release
```

The September 13-14, 2026 host checks built successfully with MSVC 19.44 / Windows SDK 10.0.26100.0. The harness exposes `--self-test`, registered through CTest. It renders deterministic reset/steady frames for static, camera-translation, camera-rotation, rigid-object, deforming-geometry and dedicated disocclusion scenarios, performs GPU -> staging readback of depth and motion, verifies frame/history metadata and motion expectations, and writes a text report plus diagnostic BMPs. The same test is run by the Windows GitHub Actions workflow.

During this validation the readback test exposed a real harness defect: the per-draw constant buffer was bound to the vertex shader but not the pixel shader, so `renderSize` read as zero in `ScenePS` and all motion vectors collapsed to zero. The fix binds `PerDraw` to both VS and PS. A second failure exposed that the intended static reference triangle was back-face culled; its winding was corrected rather than weakening the rigid-object coverage criterion.

The deterministic ground-truth run passes the current six scenarios. Visual inspection of the generated diagnostics confirms: static MV is neutral; camera translation and rotation produce continuous depth-dependent fields; rigid-object motion is isolated from the static reference geometry; and the deforming-geometry case produces a non-uniform motion field inside the deformed primitive. This is **visual-validated** evidence for the current controlled cases only, not for excluded content or reconstruction quality.

The camera+depth baseline reconstructs the current world position from hardware depth with the inverse jittered current view-projection matrix, then reprojects it through current/previous unjittered camera matrices. The deterministic comparison is **host-tested**: camera translation measures 0.000404 px mean / 0.001781 px max error, and camera rotation 0.000811 px mean / 0.004889 px max error. In the rigid-object scenario the camera remains static, so the baseline intentionally reconstructs zero object motion while renderer ground truth marks 20,686 moving pixels; all 20,686 are reported as `camera_only_miss`.

The reconstructed-motion and reconstruction-error BMPs were also inspected. Camera-motion fields are continuous, static error is effectively zero, and the rigid-object error map isolates the moving object while the static floor remains at zero. This is **visual-validated** evidence for this controlled camera-only provider and its known rigid-object limitation; it is not evidence of object-motion coverage in camera+depth reconstruction.

The harness also emits a ground-truth history-validity mask. Each visible draw writes a stable surface identity; after reprojection with renderer ground-truth MV plus the previous/current jitter delta, a current pixel is valid only if it maps in-bounds to the same surface identity in the previous frame. Resets intentionally mark every active pixel invalid. In steady state the static case keeps 232,423 of 232,455 active pixels valid; camera translation keeps 221,639/223,941 and rotation 215,564/219,379. The rigid-object case identifies 13,375 newly revealed background pixels, while the dedicated disocclusion case identifies 3,059. These checks are **host-tested** and enforced by CTest.

The generated validity masks were inspected: reusable history is green and rejected history red/yellow; the dedicated disocclusion mask places the rejected region on the floor area exposed by the translated object, while the static mask is green apart from a minimal raster/jitter boundary. This is **visual-validated** evidence for the controlled history-validity oracle, not a claim about any future optical-flow confidence estimator.

The deforming-geometry scenario evaluates a procedural per-vertex deformation using distinct current and previous deformation phases in the scene vertex shader. It is **host-tested**: the steady frame contains 36,725 moving pixels, while camera+depth reconstructs zero moving pixels and reports 36,716 `camera_only_miss` pixels. The history-validity oracle keeps 234,518/235,459 active pixels valid and identifies 799 newly revealed background pixels. The motion, reconstruction-error and history-validity BMPs were inspected and are **visual-validated** for this controlled proxy. This demonstrates that renderer ground truth can represent non-rigid vertex motion and that a camera-only provider cannot; it does not promote arbitrary skeletal animation or cloth simulation to supported coverage.

The repository now also contains an independent CPU optical-flow probe under `src/optical_flow`. It deliberately does not consume renderer depth, matrices, surface identity, or renderer motion when estimating flow. It generates textured synthetic current/previous frame pairs with known current-pixel -> previous-pixel motion and evaluates a hierarchical luminance block matcher from a 1/64 pyramid to full resolution on a 1/8 output grid. Ground truth and invalid/disoccluded masks are used only for evaluation. Confidence combines matching cost, local texture and match uniqueness.

The optical-flow scenarios are static, camera translation, camera rotation, rigid-object motion and disocclusion. The deterministic probe is **implemented** and **host-tested** through the root CTest suite. The current controlled run reports <=4 px accuracy of 100.000% static, 78.919% camera translation, 84.196% camera rotation, 95.675% rigid-object and 96.927% disocclusion; restricting evaluation to confident valid samples raises those values to 100.000%, 90.302%, 90.263%, 98.706% and 99.449% respectively. Camera translation remains the weakest case, with 4.580 px confident mean error. These values are a synthetic baseline, not evidence of production optical-flow quality or game support.

The probe writes `ltr_optical_flow_probe.txt` plus flow, confidence and error BMPs for every scenario. The disocclusion diagnostics were inspected and show confidence falling in the expected invalid/revealed region; camera translation still contains isolated and border errors. That narrow diagnostic observation is **visual-validated** for the controlled synthetic cases. No **performance-validated**, **live-tested** or game-level claim follows from this probe.

Controls are `1` static scene, `2` camera translation, `3` camera rotation, `4` rigid-object motion, `5` deforming geometry, `6` dedicated disocclusion, `Tab` diagnostic view (scene, depth, renderer motion, reconstructed motion), `R` history reset, and `Esc` exit.

Performance, real skinned/cloth, particle/transparency/HUD cases, reconstruction backends, SR, stereo validation and real-content optical-flow validation remain pending.
