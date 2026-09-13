# VR research

Status: **planned**.

The architecture must preserve one temporal input and one history per rendered view. A future VR integration should reconstruct each eye before final runtime composition rather than treating a combined stereo image as one temporal stream.

Per-view data may include color, depth, motion vectors, current and previous view/projection matrices, jitter, render/output extents, reset state, and timing metadata.

## OpenXR timing evidence

OpenXR makes timing and view identity first-class inputs rather than optional diagnostics. `xrWaitFrame` returns `predictedDisplayTime`; the specification recommends using the same target display time throughout an application-generated frame. `xrLocateViews` then returns one predicted `XrView` per view for that display time, and the runtime's recommended image dimensions are exposed per view through `XrViewConfigurationView`.

For LTR Bridge this supports keeping `predicted_display_time` and view/eye identity in the VR-side temporal metadata. It also supports reconstructing each eye after its final temporal inputs are available and before the projection layer is submitted with `xrEndFrame`. Exact placement and latency budget remain experiment-pending because a legacy VR mod may have additional rendering/reprojection stages.

The controlled stereo experiment must test independent histories, rapid camera rotation/translation, near geometry, independently moving hands or tools, animated geometry, particles, frame pacing, added latency, history resets, and binocular consistency.

It must also log the OpenXR predicted display time used for the view poses, the source of current/previous eye transforms, reconstruction start/end timing, and the image extent actually submitted for each view. This is needed to distinguish reconstruction artifacts from pose-prediction or frame-pacing errors.

No VR support claim is valid until tested on a real headset; mirror-window output is insufficient.
