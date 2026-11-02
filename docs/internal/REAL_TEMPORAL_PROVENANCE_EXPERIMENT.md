# Real temporal provenance experiment

Status: **experiment-pending** until it is run against a separately verified vanilla Call of Juarez copy.

## Purpose

Resolve the smallest real-renderer facts required to construct a coherent internal `TemporalFrame`: scene-color boundary, depth provenance/readability, shader-effective camera/projection state, actual renderer jitter, motion-vector availability or reconstructable coverage, and a common source frame/view/device/resource-generation identity. This experiment does not connect XeSS, the x86 -> x64 transport, result return, OpenXR, or headset presentation.

## Preconditions and ownership

- Use an independent vanilla Call of Juarez installation/copy. The Steam installation currently known at `C:/Program Files (x86)/Steam/steamapps/common/Call of Juarez` is externally staged by `call_of_juarez_vr` and is excluded.
- Record the absolute target path plus SHA-256 for `CoJ.exe` and `ChromeEngine3.dll` before any staging. The documented original `CoJ.exe` hash is `5EC9215E1BBDA4BE0662BEE4DF696DF35577196792CD76570DFF49F18BF109EE`; equality must be checked on the chosen copy rather than inferred from another file.
- Build the Win32 observer in a new build directory with `LTR_ENABLE_D3D9_REAL_OBSERVER_PROBE=ON`. The aggregate target must produce both `ltr_d3d9_real_observer_bootstrap.dll` and the `d3dx9_29.dll` forwarder.
- Enable only `LTR_D3D9_TEMPORAL_PROVENANCE=1` unless another observer mode is explicitly required by the experiment. Do not enable transport or XeSS.
- Before staging, write an LTR-owned run manifest outside the game directory. It must identify: run id, target path, target hashes, expected process image/path, LTR-owned staged files and their hashes, pre-existing files that must remain untouched, build directory, observer binary hashes, environment flags, start time, and cleanup status. Cleanup may remove or restore only entries owned by that manifest.
- Do not use `run_d3d9_real_observer_probe.ps1` or `CleanupOnly` as preflight. Do not launch SteamVR or a headset runtime.

## Existing bounded instrumentation

With `LTR_D3D9_TEMPORAL_PROVENANCE=1`, `DrawPrimitive` and `DrawIndexedPrimitive` emit at most 32 successful samples per source frame for at most four successful-Present frames per resource generation. Each sample records `device_id`, `source_frame_index`, `generation_frame_index`, `view_id`, `resource_generation`, draw/sample identity, RT0/depth surface identity and descriptors, viewport, vertex/pixel shader identity, up to 64 VS float4 constants as a deterministic hash, and literal `c0..c15` values. Successful `Present` advances the source frame. Successful `Reset` starts a new generation-local frame sequence while source frame identity remains monotonic.

These observations are **implemented** and compile/host-tested only. A depth descriptor is not proof that depth is shader-readable or semantically linear, and shader constant observation is not proof that a register is camera/projection state.

The bounded observer also records one `temporal_provenance_present` event per
`source_frame_index` for the first four frames of each resource generation. The
record is captured immediately before the real `Present` advances temporal
identity and contains the pre-Present RT0 identity/descriptor, swapchain
backbuffer identity/descriptor, source/destination rects, the real Present
HRESULT, and the same `(device_id, source_frame_index, view_id,
resource_generation)` join key as draw samples. This is observational metadata;
it does not capture image contents or prove an offscreen target was copied into
the backbuffer.

## Capture boundary and identity

The experiment uses this join key before any temporal inputs may be associated:

`(device_id, source_frame_index, view_id, resource_generation)`

`generation_frame_index` is diagnostic within a resource generation. `transport_index` is not part of provenance and must not substitute for `source_frame_index`. A reset increments the existing device resource generation. Cross-device, cross-view, cross-generation, stale-frame, and source-frame-gap joins are failures for history reuse unless explicitly classified as a reset/cut/gap by the internal temporal contract.

## Questions and acceptance criteria

### 1. Scene color

**Accept** when sampled draws identify a stable RT0/viewport sequence for world rendering and same-frame `temporal_provenance_present` records demonstrate how that sequence reaches the pre-Present RT0/swapchain backbuffer. A direct identity match is sufficient for the observed frame; if the selected world RT differs, an observed later pass/copy relationship is still required. Record whether HUD/post effects are inside or outside the selected boundary.

**Partial** when the likely scene target is identified and its same-frame Present-side identities are known, but the route between an offscreen scene RT and the backbuffer or HUD/post-process inclusion cannot be separated.

**Fail** when multiple incompatible RT0 candidates cannot be ordered, the selected target crosses source frames/generations, or capture would require mixing unrelated passes.

### 2. Depth

**Accept** only when the depth surface paired with the selected scene draws has a demonstrated shader-readable extraction path and its convention/range is checked against visible geometry or another controlled geometric invariant.

**Partial** when draw-time depth identity, descriptor and viewport pairing are coherent but readable extraction or semantics remain unresolved. Label this as bound-depth provenance only.

**Fail** when depth changes independently of the chosen scene pass, crosses frame/view/generation identity, or cannot be associated with the selected color boundary.

### 3. Effective camera/projection constants

Use controlled camera-only motion and stationary-scene samples. Group by vertex-shader identity and compare constant values/hashes across draws and frames. Candidate camera/projection registers must evolve consistently with camera motion, remain coherent across draws that share the selected scene boundary, and reset/resize consistently with the same resource generation.

**Accept** when a concrete register set and convention are supported by draw-time evidence. **Partial** when camera-correlated constants are found but object/world transforms cannot yet be separated. `GetTransform` alone is not acceptance evidence.

### 4. Jitter

Observe the accepted projection/camera constants over consecutive source frames with the camera static and moving.

**Accept** only if a recurring subpixel projection offset is identified with units/sign/cadence and can be paired to the same source frame/view as color/depth. **Observed no jitter** is a valid result if the accepted projection state remains stable at subpixel terms over the bounded sample. **Fail/unresolved** if candidate changes cannot be distinguished from camera/object transforms.

### 5. Motion vectors and coverage

First determine whether the renderer exposes any native motion-vector target/pass in the sampled draw stream. If none is demonstrated, evaluate camera+depth reconstruction only after camera/depth acceptance.

Coverage must explicitly exclude unresolved rigid-object motion, deforming/skinned geometry, particles, transparency, HUD/post effects and disocclusion. Optical flow is not assumed or introduced by this experiment.

### 6. Temporal integrity negatives

Offline contract checks must continue rejecting stale frames, source-frame gaps for history reuse, cross-view joins, resource-generation changes, invalid/mismatched subrects, non-finite delta/jitter, depth/MV semantic mismatches and output ownership mismatches. A live reset must start a new generation-local sequence without reusing prior-generation history.

## Execution record

For a live run, preserve outside ephemeral build directories:

- repository HEAD plus `git diff --binary` or equivalent working-tree snapshot;
- game binary hashes and LTR-staged file hashes;
- Windows build, GPU/driver, MSVC, SDK and CMake versions;
- exact environment flags and command line;
- timeline with process id/path and Present/Reset generation transitions;
- provenance log plus validation mode and sample count;
- criteria table marking each question **verified**, **observed**, **hypothesis**, **experiment-pending**, **implemented**, **host-tested**, or **live-tested** as justified;
- cleanup result proving only manifest-owned files were restored/removed.

## Stop gate

Do not connect the real frame to transport/XeSS unless scene color, temporal identity, camera provenance and at least bounded depth provenance are coherent. If depth readability, jitter, or MV/object coverage remain partial, record those exclusions and keep them explicit in `TemporalFrame` validity/history semantics. The next gate after coherent real inputs is the existing x86 -> x64 path with XeSS 3.0.2 Native AA at 1:1 and safe original presentation on failure.
