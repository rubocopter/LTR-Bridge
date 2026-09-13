# Motion-vector research

Status: central unresolved problem.

Candidate sources, in descending expected fidelity:

1. a native velocity buffer already produced by the renderer;
2. motion derived from current and previous renderer transforms;
3. depth plus camera matrices for rigid world geometry;
4. image-space motion estimation as a fallback experiment.

Each source must record its direction convention, scale/units, resolution, whether camera jitter is included, whether vectors are dilated, provenance, known exclusions, and validation state.

Depth-and-camera reconstruction is expected to miss independently moving or animated content unless additional information is supplied. Image-space estimation must not be treated as equivalent to renderer-native motion without validation.

The controlled test set should include a static view, camera translation, camera rotation, independently moving geometry, animation, disocclusion, particles, UI, and fast near-camera motion. Stereo/head-motion cases belong to the later VR harness.
