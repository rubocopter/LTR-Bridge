# VR research

Status: **planned**.

The architecture must preserve one temporal input and one history per rendered view. A future VR integration should reconstruct each eye before final runtime composition rather than treating a combined stereo image as one temporal stream.

Per-view data may include color, depth, motion vectors, current and previous view/projection matrices, jitter, render/output extents, reset state, and timing metadata.

The controlled stereo experiment must test independent histories, rapid camera rotation/translation, near geometry, independently moving hands or tools, animated geometry, particles, frame pacing, added latency, history resets, and binocular consistency.

No VR support claim is valid until tested on a real headset; mirror-window output is insufficient.
