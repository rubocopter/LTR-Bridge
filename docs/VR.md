# VR research

Status: **planned; OFXR Bridge presentation architecture reviewed, controlled integration still experiment-pending**.

The architecture must preserve one temporal input and one history per rendered view. A future VR integration should reconstruct each eye before final runtime composition rather than treating a combined stereo image as one temporal stream.

Per-view data may include color, depth, motion vectors, current and previous view/projection matrices, jitter, render/output extents, reset state, and timing metadata.

## OpenXR timing evidence

OpenXR makes timing and view identity first-class inputs rather than optional diagnostics. `xrWaitFrame` returns `predictedDisplayTime`; the specification recommends using the same target display time throughout an application-generated frame. `xrLocateViews` then returns one predicted `XrView` per view for that display time, and the runtime's recommended image dimensions are exposed per view through `XrViewConfigurationView`.

For LTR Bridge this supports keeping `predicted_display_time` and view/eye identity in the VR-side temporal metadata. It also supports reconstructing each eye after its final temporal inputs are available and before the projection layer is submitted with `xrEndFrame`. Exact placement and latency budget remain experiment-pending because a legacy VR mod may have additional rendering/reprojection stages.

The controlled stereo experiment must test independent histories, rapid camera rotation/translation, near geometry, independently moving hands or tools, animated geometry, particles, frame pacing, added latency, history resets, and binocular consistency.

It must also log the OpenXR predicted display time used for the view poses, the source of current/previous eye transforms, reconstruction start/end timing, and the image extent actually submitted for each view. This is needed to distinguish reconstruction artifacts from pose-prediction or frame-pacing errors.


## OFXR Bridge presentation evidence

OFXR Bridge was reviewed at `tig3rmast3r/OFXR-Bridge@dad56acafc6e1dde219940427738b926cf2ea555`. **Verified from source:** it demonstrates an OpenXR implicit-layer route for inserting a synthetic frame while keeping projection views, private swapchain resources, runtime-facing frame calls and D3D11/D3D12 interop under explicit ownership. Its implementation also contains dedicated handling for pipelined application frame loops and SteamVR pacing.

**Verified limitation:** its normal generation path is color-only optical flow; it does not receive game depth or renderer motion vectors, and the source states that translation compensation remains depth-unaware. Upstream documents artifacts around moving objects, disocclusions and head rotation. This makes it useful as presentation/pacing evidence and as a color-only baseline, not as evidence that LTR Bridge can omit depth/MV extraction.

The controlled stereo/OpenXR harness should therefore cover swapchain recreation, recentering, space destruction, pipelined `xrWaitFrame`/`xrBeginFrame` behavior and physical-headset presentation in addition to temporal image quality. See `docs/CASE_STUDY_OFXR_BRIDGE.md`.

No VR support claim is valid until tested on a real headset; mirror-window output is insufficient.
