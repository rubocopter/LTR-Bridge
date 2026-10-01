# Provisional temporal contract

Status: **hypothesis; do not freeze ABI yet**.

The contract must describe temporal meaning separately from transport representation. A backend adapter should be able to reject missing or semantically invalid fields rather than accepting a texture with the wrong convention.

## Conceptual frame

```text
TemporalFrameInput
  identity:
    resource_generation
    frame_index
    camera_or_view_id?
    history_reset
    eye_id?                 # VR only
    predicted_display_time? # VR only
    frame_delta_ms?

  extents:
    render_width/height
    output_width/height
    color/depth/motion valid subrects

  color:
    resource
    format
    color_space
    pre_exposure/exposure?

  depth:
    resource
    convention
    near/far?
    inverted?
    infinite_far?
    linear?

  motion:
    resource
    resolution
    scale
    direction
    jittered?
    dilated?
    provenance
    coverage?
    known_exclusions?
    confidence_or_validity?
    producer_stream_id?

  camera:
    view/projection
    previous view/projection or clip transforms
    jitter_x/y

  optional masks:
    reactive
    transparency/composition
    responsive pixels

  provenance:
    native | reconstructed | intercepted | optical-flow | unavailable
```

This is a semantic model, not a C++ layout.

The 2026-09-23 review of `Beren5556/W40KRT_VR@929bc9c1626b92d102def4ab405bf943c83bab24` provides additional **observed** evidence for these fields. Its per-eye NGX job ABI identifies generation/frame/camera/eye and carries subrects, jitter and MV scale; its optional OFXR provider V2 additionally publishes depth convention, frame delta, reset and stream identity and explicitly rejects stale/queue-mismatched pairs. Those concrete layouts are implementation-specific evidence, not the LTR Bridge ABI.

The 2026-09-30 recheck at `Beren5556/W40KRT_VR@0fb98a2f991075256dffd2117f9d458360caa324` adds a second identity boundary: reconstruction configuration has its own generation/status, and the OFXR join now rejects neural-backed synthesis unless backend generation and both completed eye-frame IDs match the game frame. **Observed:** downstream consumers need explicit producer/backend readiness in addition to frame-resource identity; model/preset choice remains backend configuration rather than a temporal-frame field.

## Separation of concerns

### Renderer information

What the game actually rendered: resources, matrices, frame boundaries, internal resolution, view identity, and reset events.

### Temporal derivation

How missing information was produced: native velocity target, depth reprojection, per-draw transform capture, optical flow, etc.

### Transport

How the resource reaches the executing backend: same device, shared handle, relay device, GPU copy, or another mechanism.

### Backend mapping

How semantic fields become DLSS/Streamline, FidelityFX, or XeSS parameters and resource tags.

## Cross-backend comparison

| Semantic input | DLSS / DLAA | FidelityFX temporal upscaling | XeSS-SR / Native AA | Contract decision |
| --- | --- | --- | --- | --- |
| Input color | required | required | required | core |
| Output resource/extent | required | required | required | core |
| Depth | required in normal integration | required | required when using low-res MVs; otherwise still useful | core, with validity metadata |
| Motion vectors | required | required | required | core |
| MV scale/convention | required | configurable/required | configurable/required | core |
| Jitter | required for proper temporal SR/AA | required | required | core |
| History reset | required conceptually | required conceptually | available/required conceptually | core |
| Camera matrices | Streamline common constants include transforms | implementation-dependent but useful to provider | backend/FG paths use frame constants; SR can operate from supplied resources | provider/core metadata, not assume every backend consumes identical matrices |
| Exposure | explicit or auto | optional/auto | exposure multiplier/paths available | optional extension |
| Reactive/transparency mask | backend/version dependent | supported and important for some content | responsive-pixel mask | optional typed masks |
| Internal/output resolution distinction | required for SR modes | required for SR modes | required for SR modes | core |

The first concrete backend mapping is now **host-tested** with XeSS SDK `v3.0.2` / `8fe81bdb...`: `XESS_QUALITY_SETTING_AA` reports a 1.0x input extent, and `XESS_INIT_FLAG_RESPONSIVE_PIXEL_MASK` plus `pResponsivePixelMaskTexture` provides a real typed mapping for content whose history validity is not represented by geometry depth/MV. The XeSS probe now shares the harness's Halton-8 jitter and current-to-previous render-pixel MV convention, with jitter excluded and high-resolution MV dilated. In that coherent temporal case, the responsive path reduced final moving-HUD changed-region MAE from `8.3259` to `5.6902` on the local RTX 4070 Ti. This establishes one backend-specific mapping; it does not convert `reactive` into a mandatory universal mask or settle FidelityFX/DLSS semantics.

## Motion-vector semantics

The contract must never store only `TextureHandle mv`.

Minimum metadata should state:

- current-to-previous or previous-to-current convention;
- pixel, UV, NDC, or already-normalized units;
- scale to backend convention;
- render-resolution or output-resolution grid;
- whether jitter is included;
- whether vectors are dilated;
- provider provenance;
- content coverage / known exclusions;
- confidence/validation state.

For reconstructed/optical-flow providers, confidence should be representable as an explicit per-pixel resource or typed mask rather than only a global provider label. Current legacy experiments already benefit from rejecting vectors using luma/depth/consistency tests; the contract should not force those diagnostics to be discarded before backend mapping.

Native provenance does not imply complete motion coverage. The Rogue Trader renderer-native case study exposes camera and object motion-vector passes but still has known cloth/cape omissions that produce ghosting. Coverage therefore needs to remain independently describable even for a native renderer buffer.

## Depth semantics

Minimum metadata should state:

- hardware/non-linear vs linear depth;
- normal or reversed/inverted convention;
- sample count;
- valid viewport/subrect;
- relation to the motion vectors;
- near/far when required to reconstruct/linearize.

## Jitter

The contract should store jitter separately from projection matrices. NVIDIA Streamline explicitly expects camera matrices without jitter and a separate pixel-space jitter constant; XeSS likewise documents explicit subpixel jitter.

For a legacy game with no native TAA, the adapter/provider must determine where projection jitter can be introduced without breaking HUD, first-person weapon rendering, shadows, post-process passes, cinematics, or VR projection.

`jitter = 0` is not a generic substitute for a real temporal-SR integration.

Likewise, shifting a post-process/downsample sampling grid after the game has rendered is not equivalent to jittering the renderer projection. It may be a useful reconstruction experiment but must carry different provenance so it cannot be mistaken for real renderer-generated subpixel samples.

## History identity

History reset should be triggered by more than a resize. Candidate reasons include:

- camera cut/teleport;
- projection mode/FOV discontinuity;
- render or output extent change;
- backend recreation;
- depth/MV provider loss or convention change;
- entering/exiting a menu or video path when game frames are no longer temporally continuous;
- eye target recreation in VR.

History/resource generation should also be monotonic across recreation so a delayed job or guide cannot become valid merely because a pointer/slot was reused. Where temporal inputs can arrive through different producers, pair them by generation/frame/view (or an equivalently strong stream identity) and reject stale or ownership-mismatched data.

## VR extension

VR requires at minimum an `eye_id` or equivalent view identity. Histories must not be accidentally shared between eyes. Predicted display time and pose provenance may become necessary for diagnosing head-motion vectors and latency, but should not be required in the first flat-screen ABI.

The W40KRT_VR beta provides **observed** source evidence for treating this as ownership as well as metadata: its left/right TAA state is independent and history seeding first verifies that the live camera and camera-buffer owner match the eye being processed. A future stereo probe should include an explicit ownership-mismatch rejection test in addition to checking that left/right pixels differ as expected.

## ABI decision gate

Do not define a stable public `TemporalFrameInput` C ABI until all of these exist:

1. controlled DLAA or equivalent native-AA backend mapping — **XeSS Native AA first mapping host-tested; broader semantics still pending**;
2. FidelityFX temporal backend mapping;
3. XeSS Native AA or SR mapping;
4. one x86 transport prototype;
5. one D3D10 or D3D9 provider that exposes genuinely missing/legacy-specific information;
6. a stereo prototype showing how view identity/history extends the model.
