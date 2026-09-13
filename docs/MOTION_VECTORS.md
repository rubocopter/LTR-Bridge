# Motion-vector research

Status: central unresolved problem.

Candidate sources, in descending expected fidelity:

1. a native velocity buffer already produced by the renderer;
2. motion derived from current and previous renderer transforms;
3. depth plus camera matrices for rigid world geometry;
4. image-space motion estimation as a fallback experiment.

Each source must record its direction convention, scale/units, resolution, whether camera jitter is included, whether vectors are dilated, provenance, known exclusions, and validation state.

Depth-and-camera reconstruction is expected to miss independently moving or animated content unless additional information is supplied. Image-space estimation must not be treated as equivalent to renderer-native motion without validation.

## Current ReShade ecosystem evidence

LumeniteFX currently provides a useful reference implementation of image-space motion estimation. Its Kernel pre-effect computes reconstructed normals, motion vectors and a motion-confidence output for later temporal effects. Current DLSS5-Feeder recommends Lumenite Kernel and consumes a 1/8-resolution flow plus confidence map; other supported providers include optical-flow implementations such as VORT and iMMERSE Launchpad.

This is strong evidence that generic legacy motion estimation can be useful enough to drive temporal effects, but it also exposes its ceiling. Feeder documents ghosting in fast motion, softness on thin moving geometry, problems around flames/transparency, and HUD contamination. Its experimental geometry-vector mode fits camera motion from optical flow plus depth and is explicitly described as noisy; upstream notes that doing it properly requires the game's real view-projection matrices.

## Validation and confidence

An estimated-vector provider should produce more than an RG texture. The current ecosystem already demonstrates useful diagnostics that LTR Bridge should generalize:

- confidence or validity per pixel;
- non-zero share and magnitude statistics;
- luma consistency across the proposed correspondence;
- depth consistency / disocclusion rejection;
- static-scene hypothesis checks;
- explicit provider identity, resolution, direction and units.

Invalid/rejected motion should become an explicit mask or confidence field so a backend/provider can choose how to treat history rather than silently feeding bad vectors.

## Renderer-derived hierarchy

The preferred long-term path remains renderer evidence rather than optical flow:

1. use a native velocity buffer when semantically understood;
2. capture current/previous object and camera transforms and produce per-draw/per-pixel motion;
3. reconstruct camera/static-world motion from depth + real matrices, with separate handling for independently moving geometry;
4. use image-space motion estimation as a fallback or diagnostic source.

This hierarchy is especially important for VR: head motion is known from tracking and controller/weapon motion may be known independently, so discarding that information and re-estimating everything from color would add avoidable latency/noise.

The controlled test set should include a static view, camera translation, camera rotation, independently moving geometry, animation, disocclusion, particles, UI, and fast near-camera motion. Stereo/head-motion cases belong to the later VR harness.
