# OFXR Bridge OpenXR frame-generation case study

Status: **verified from source / observed upstream; integration experiment-pending**.

Reviewed upstream: `tig3rmast3r/OFXR-Bridge` at commit `dad56acafc6e1dde219940427738b926cf2ea555` on 2026-09-16; upstream status rechecked 2026-09-30. The upstream README identifies the current pre-release as `v0.2.1` (internal build `V116`). The recheck found the same commit and release, so the base OFXR findings below have not moved.

This case study is relevant to the VR/output side of LTR Bridge. OFXR Bridge is an experimental OpenXR implicit API layer that inserts an optical-flow-generated frame between application-rendered frames. It does not provide evidence that color-only optical flow is sufficient for LTR Bridge's temporal-data problem; instead it provides concrete implementation evidence for the OpenXR presentation, pacing, swapchain and per-view lifecycle problems that occur after a synthetic frame exists.

## Findings

1. **Verified from source:** OFXR Bridge intercepts the OpenXR frame lifecycle and maintains runtime-facing presentation state around `xrWaitFrame`, `xrBeginFrame`, `xrEndFrame`, swapchain ownership and projection-layer snapshots. This is direct evidence that an OpenXR API layer can be a separable output/presentation stage for generated frames.
2. **Verified from source:** the implementation supports native D3D12 resources and a D3D11 -> D3D12 interop path. Its frame-generation state uses private current/synthetic swapchains and explicit lifetime/synchronization handling rather than assuming application swapchain images remain valid indefinitely.
3. **Verified from source:** logical stereo views are kept distinct. The synthesizer uses independent mono history/resources per view and the NVIDIA optical-flow path avoids treating both eyes as one repeated image atlas because upstream observed cross-image matches with that approach.
4. **Verified from source:** pose/FOV are part of synthesis. The synthesizer subtracts the image-space component attributable to OpenXR orientation/FOV mapping before applying residual scene motion. The same source explicitly states that translation remains depth-unaware.
5. **Observed upstream:** current OFXR Bridge uses color-only optical flow and does not receive game depth or renderer motion vectors. Its README warns about artifacts around moving objects, disocclusions and head rotation. This independently supports LTR Bridge's decision to treat optical flow as a fallback/baseline rather than equivalent to renderer-derived temporal data.
6. **Verified from source:** runtime integration required handling pipelined frame loops, SteamVR pacing, recentering, swapchain recreation and `XrSpace`/resource destruction. These are concrete failure classes for LTR Bridge's future controlled stereo/OpenXR experiment.
7. **Observed upstream:** OFXR Bridge currently exposes AMD FidelityFX Optical Flow and NVIDIA Optical Flow backends and reports compatibility work for UEVR, UEVR Native Stereo, Luke Ross mods and SteamVR. These are upstream compatibility observations, not LTR Bridge support claims.
8. **Verified licensing/provenance:** OFXR Bridge is LGPL-3.0-or-later. Its third-party documentation pins OpenXR headers and records separate AMD/NVIDIA component terms. Treat it as an architectural/source reference unless a future reuse decision explicitly evaluates LGPL and third-party obligations.

## Architectural implication

The case study strengthens a VR-side decomposition such as:

`Temporal provider -> reconstruction/frame synthesis -> OpenXR presentation layer -> runtime -> HMD`

This complements the BioShock VR case study rather than replacing it. BioShock provides evidence for game-specific temporal extraction, x86/x64 transport and independent eye histories; OFXR Bridge provides evidence for generic OpenXR interception, synthetic-frame presentation, per-view resource handling and runtime pacing.

**Hypothesis:** a future LTR Bridge VR path may be able to keep its renderer-specific temporal provider and reconstruction backend independent from an OpenXR presentation component. That should be tested in the controlled stereo harness before any reusable runtime architecture is selected.

## Experiment requirements imported into LTR Bridge

The controlled OpenXR/stereo experiment should explicitly test:

- two independent eye histories/resources and cross-eye contamination detection;
- `predictedDisplayTime`, per-view pose/FOV identity and synthetic-frame target timing;
- application loops with pipelined waits/begins rather than assuming one strictly serial frame loop;
- swapchain recreation, resize, recenter and space destruction;
- D3D11 -> modern-host interop ownership boundaries where applicable;
- runtime pacing and added latency separately from reconstruction cost;
- physical-headset presentation, because accepted `xrEndFrame` submissions alone do not prove headset scanout.

## Evidence boundary

**Verified:** source architecture and the implementation properties listed above at the pinned commit.

**Observed:** upstream compatibility/performance/visual-artifact statements from the reviewed README/release notes.

**Experiment-pending:** any use of OFXR Bridge code or ABI, any LTR Bridge OpenXR layer, physical-headset validation, and whether LTR Bridge temporal inputs materially improve the failure cases seen by color-only OFXR generation.

## 2026-09-23 follow-up — temporal-guide provider in W40KRT_VR

`Beren5556/W40KRT_VR@929bc9c1626b92d102def4ab405bf943c83bab24` vendors a modified optional OFXR provider in its `v0.9.79-beta` source. This does not supersede the upstream snapshot above, but it exposes a useful producer/provider contract that was not part of the earlier LTR Bridge review.

**Observed from pinned source:** `include/xrfg/provider_api.hpp` defines a versioned process-local provider ABI. The OpenXR layer owns state and GPU snapshot lifetime; producers publish temporal guides and control requests. The V2 guide publication carries a stream identity, output and motion resources, producer queue/list, output/MV subrects, MV scale, jitter, reset, depth resource/subrect/state, frame delta, near/far, depth inversion/infinite flags and an optional identity-checked native device. Statistics distinguish missing streams, stale pairs, queue mismatches, temporal rejection, invalid rejection and successful use.

**Observed from pinned source:** `d3d12_history.hpp` uses a three-slot history ring with capture tickets and exclusive consumer leases. Capture signals a fence and never waits for GPU completion on the release path; if a ring slot is still owned it returns busy and the caller must fail open rather than overwrite history. A consumer must retire its lease with a completion fence or cancel it before GPU access.

**Observed from pinned source:** `d3d11_d3d12_interop.hpp` creates a private D3D12 device/queue on the D3D11 OpenXR adapter and uses a shared timeline fence to order D3D11 <-> D3D12 copies without per-frame CPU waits. This is same-process, same-adapter interop and is not evidence for LTR Bridge's x86-process -> x64-process transport.

**Observed from pinned source:** NVIDIA frame-generation backpressure enters a cooldown if synthetic completion has remained outstanding for more than the greater of six display periods or 75 ms. This is a presentation-specific recovery heuristic, not a universal threshold.

### Implications for LTR Bridge

1. Keep `stream/resource generation`, subrects, jitter, MV scale/convention, depth convention and frame delta explicit in the provisional temporal model.
2. Reject stale or ownership-mismatched temporal guides rather than silently pairing them with a newer color frame.
3. Consider ticket/lease ownership for the future x64 reconstruction-history stage; do not replace the already host-tested two-slot x86/x64 transport until the real-game gate gives a reason.
4. Keep producer, history/reconstruction and OpenXR presentation lifetimes independently retireable.
5. Treat display-period-aware cooldown as a candidate pattern for future VR frame-generation work in the other mods, to be validated per runtime/backend.

Licensing remains material: the optional provider is LGPL-3.0-or-later, AMD FidelityFX material is MIT, and NVIDIA components retain their own terms. These findings are architectural evidence, not a decision to copy provider source into LTR Bridge.

## 2026-09-30 follow-up — W40KRT_VR 0.9.81 tightens the OFXR join

The upstream `tig3rmast3r/OFXR-Bridge` snapshot above is unchanged, but the modified provider carried by `Beren5556/W40KRT_VR` has advanced to `v0.9.81-beta` / `0fb98a2f991075256dffd2117f9d458360caa324`.

**Observed from pinned source:** when neural reconstruction participates in the frame, the provider now queries the shipped neural backend's own `RTN_BackendStatus` and requires `READY`, no backend failure reason, the same neural generation, and completed left/right neural frames matching the game frame before OFXR synthesis is eligible. The OpenXR layer records separate preparation reasons for an ineligible host descriptor and an incompatible neural frame.

**Architectural implication:** temporal-guide eligibility and reconstruction completion are separate gates. A future LTR Bridge VR presentation stage should consume a reconstructed/temporal frame only when descriptor identity, backend generation/status and per-view completion all agree, and should preserve the reason when one gate fails. This strengthens the existing stale-pair/ownership rejection rule without changing the host-tested x86/x64 transport design.
