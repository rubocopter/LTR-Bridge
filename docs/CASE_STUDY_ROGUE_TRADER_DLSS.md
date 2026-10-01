# Case study — Rogue Trader DLSS / EnhancedGraphics

Research dates: 2026-09-13; public-VR-beta follow-up 2026-09-23; upstream recheck 2026-09-30.

Public upstream targets reviewed:

- Original repository: https://github.com/cstamford/RogueTrader_DLSS
- Original pinned commit: `f2444b09ecee649e39851715cb5133e7020e159c` (`1.0.1`).
- Actively revised public fork: https://github.com/BradyBrenot/RogueTrader_DLSS
- Fork release: `v2.2`, published 2026-06-13.
- Fork release/source commit: `01b1cd816db08f2b1c6c68b1319c6f44dd61bdd6`.
- Fork README states testing against Rogue Trader `1.5.0.320`.

This public implementation is relevant because, unlike a finished-frame injector, it modifies Owlcat's Unity/Waaagh render pipeline early enough to control internal render resolution, projection jitter, motion-vector production and the position of the reconstruction pass relative to post-processing.

It is not assumed to be the same private/local Rogue Trader work as any other modder's implementation unless that source is provided separately.

## Renderer-native temporal inputs

**Verified from source:** the injected `UpscalePass` reads the engine's `CameraColorBuffer`, `CameraDepthBuffer` and `CameraMotionVectorsRT` directly from the render graph. It also receives the current camera jitter and supplies motion-vector scale derived from render resolution.

The mod disables the game's normal AA path for the affected camera but explicitly enqueues both `CameraMotionVectorsPass` and `ObjectMotionVectorsPass` so motion data remains available even though the original TAA path is disabled.

This is materially different from the BioShock case study:

- BioShock currently reconstructs camera-only motion from depth + camera state and uses zero projection jitter.
- Rogue Trader uses the renderer's own camera/object motion-vector path and injects real temporal projection jitter into the camera buffer.

**Conclusion:** the two projects form useful reference points on a temporal-quality ladder rather than competing architectures.

## Real projection jitter

**Verified from source:** the fork patches the Waaagh camera-buffer update and replaces the normal jitter state for the upscaled camera. It writes jitter offset, jitter UV and a jitter matrix derived from display resolution, render resolution and frame index before scene rendering.

Commit `94a370d7b9e9e00c74699c1e73a08e23779e16a2` specifically records a jitter-scaling correction that became visible in Performance/Ultra Performance modes with newer DLSS versions.

**Relevance:** this is strong evidence for keeping renderer jitter and backend jitter as one coupled semantic operation. It also confirms that errors which are subtle at DLAA/native resolution can become obvious at aggressive SR ratios.

## Internal render resolution and pass ordering

**Verified from source:** the mod patches both scaled and non-scaled camera viewport sizes so the scene can render below display resolution while the final target remains at display resolution. It inserts the DLSS pass before post-processing and patches post-process target descriptors so post-processing executes at full output resolution.

The implementation also moves the highlighting pass before the upscale pass because leaving it later caused instability/quality loss.

**Conclusion:** real SR is not only `renderWidth < outputWidth`. The provider must know which passes are scene-resolution, which are output-resolution and which content should be included in temporal reconstruction at all.

LTR Bridge should therefore treat reconstruction insertion point and render-stage classification as provider/profile responsibilities, not backend details.

## Motion-vector coverage is separate from provenance

**Verified:** the engine supplies camera and object motion-vector passes, but the current public fork still has an open issue for cloth/cape ghosting because cloth is not represented correctly in those motion vectors.

This is an important qualification to the provisional contract: a motion resource can be genuinely **native** while still having incomplete content coverage.

The contract should therefore distinguish at least:

- provenance: native / reconstructed / optical-flow / unavailable;
- coverage: camera-only, rigid-world, object-aware, or otherwise explicitly described;
- known exclusions: cloth, particles, transparencies, first-person geometry, etc.;
- validation/confidence state.

`native` must not be interpreted as `complete`.

## Resolution-dependent engine assumptions

**Verified from the 2026 fork history:** reducing internal resolution exposed a particle-size bug because some transparent/billboard shaders consumed `_ScreenParams` rather than a render-scale-aware value. The fork fixed this by correcting the global screen parameters immediately before transparent rendering. It also adjusts global mip bias for the selected render/output ratio.

**Conclusion:** controlling internal resolution can invalidate engine assumptions far beyond the backbuffer size. A real legacy-SR provider may need profile-specific fixes for:

- screen-space particle/billboard sizing;
- texture mip bias;
- post-process target sizes;
- HUD/highlight placement;
- reflection/shadow/secondary-camera policy;
- any shader constants derived from nominal screen size.

This strengthens the rule that renderer-resolution control belongs in the per-engine/per-game provider layer.

## D3D11 execution without process hooking

The original implementation used a more invasive native interception route. The 2026 fork deliberately removed process hooking, MinHook and the native debug UI.

**Verified from current source:** managed code queues evaluation data, then uses Unity's `IssuePluginEventAndData` so a native render-thread callback performs the pending NGX D3D11 evaluation at the appropriate point. The native side initializes from the game's D3D11 resource/device and invokes NGX directly on that device.

**Relevance:** when the engine exposes a safe render-thread plugin mechanism, a smaller integration surface is preferable to generic process/API hooking. LTR Bridge should not generalize this to legacy engines that lack such a mechanism, but it is a useful example of choosing the least-invasive adapter available.

## History/reset limitation

**Observed from current source:** the NGX reset bit is driven primarily by feature creation/recreation (`Dirty`) rather than a rich camera-cut/teleport/history-discontinuity policy like the BioShock provider uses. The managed `UpscalePass` currently passes `Reset = false` and that field is not propagated into the native evaluation structure.

This is another useful contrast: Rogue Trader has stronger renderer-native temporal inputs, while BioShock currently has stronger explicit temporal identity/rejection/reset discipline.

LTR Bridge should combine both strengths in its harness rather than copying either implementation wholesale.

## Backend substitution evidence

The current fork documents OptiScaler use on the DLSS input path so non-NVIDIA users can route the game's temporal inputs to other reconstruction implementations such as XeSS or newer FSR variants.

**Observed implication:** once a game exposes a sufficiently coherent temporal contract, backend substitution becomes much easier. This supports LTR Bridge's backend-separation goal, but does not prove that all backend-specific semantics are interchangeable.

## Validation value for LTR Bridge

Rogue Trader is useful as a **positive-control renderer-native case study**:

1. compare native camera+object motion against camera+depth reconstruction and optical flow;
2. validate correct/incorrect jitter scale at multiple SR ratios;
3. observe known incomplete native MV coverage using cloth/capes;
4. test scene-resolution vs output-resolution pass placement;
5. test resolution-dependent particle/billboard and mip-bias behavior;
6. compare DLAA 1:1 against actual sub-native DLSS SR using the same semantic inputs.

It should not replace the synthetic D3D11 harness: the harness remains necessary to establish known ground truth independently of game code.

## Licensing/provenance

**Verified:** the current fork references NVIDIA's public DLSS repository as a submodule, while NGX/DLSS binaries remain governed by NVIDIA terms.

**Important:** no root repository license was detected in either reviewed Rogue Trader repository snapshot. Public source availability is not, by itself, permission to copy/relicense implementation code. Until the author supplies explicit licensing terms, LTR Bridge should treat these repositories as architectural/reference evidence and avoid source reuse.

## What this changes for LTR Bridge

1. Add **motion coverage/exclusions** to the provisional semantic model; native provenance alone is insufficient.
2. Add an explicit **render-stage/insertion-point** concern to provider validation for real SR.
3. Extend the D3D11 harness with a quality ladder: native camera+object MV -> camera+depth reconstruction -> optical flow.
4. Validate jitter at more than one SR ratio, not only DLAA/native resolution.
5. Add screen-space/resolution-assumption cases (particles, HUD/highlights, mip bias) to real-SR tests.
6. Keep BioShock and Rogue Trader as complementary references: BioShock for x86/x64 stereo transport and strict temporal identity; Rogue Trader for renderer-native resolution/jitter/MV integration.

## Primary upstream files/evidence reviewed

Pinned to `BradyBrenot/RogueTrader_DLSS@01b1cd816db08f2b1c6c68b1319c6f44dd61bdd6` unless otherwise noted:

- `README.md`
- `Mod/Game/Passes.cs`
- `Mod/Game/Patches.cs`
- `Mod/Upscalers/DLSS.cs`
- `Native/src/Interop.cpp`
- `Native/src/Services/DLSS.cpp`
- `.gitmodules`
- release `v2.2`
- issue `#2` (cloth ghosting)
- commit `94a370d7b9e9e00c74699c1e73a08e23779e16a2` (no-hook rework + jitter scaling)
- commit `5051ac4ee4c4e399f745d07cf99d04326e24e5c0` (particle screen-parameter fix)

Original architecture baseline also reviewed at `cstamford/RogueTrader_DLSS@f2444b09ecee649e39851715cb5133e7020e159c`.

## 2026-09-23 follow-up — W40KRT_VR public beta

Public upstream reviewed: https://github.com/Beren5556/W40KRT_VR, release `v0.9.79-beta`, published 2026-09-22, source commit `929bc9c1626b92d102def4ab405bf943c83bab24`.

This is a separate VR project from the EnhancedGraphics fork above. The beta targets the 64-bit DirectX 11 Steam build, drives OpenXR directly, exposes per-eye TAA/DLSS/DLAA and includes optional OFXR frame generation. Its architecture is useful to LTR Bridge because it makes several temporal and lifetime contracts explicit in shipping beta source.

### Per-eye temporal identity and fail-open reconstruction

**Observed from pinned source:** `src/native/core/neural/Neural.h` defines each neural job with `generation`, `frame`, `cameraId` and `eye`, plus color, depth, motion, render/output extents, subrect origins, jitter, motion-vector scale, exposure and frame time. Configuration changes advance a nonzero generation and retire the previous feature state.

The same ABI uses a strong fail-open rule: submission copies metadata and AddRefs resources, evaluation happens later on the Unity render thread, the current raw image is already present in the destination, NGX writes into private scratch, and only a validated successful result may replace the destination. Queue-full, stale, resource, runtime, device-lost and evaluation failures are explicit states rather than reasons to corrupt the current frame.

**Relevance:** LTR Bridge should carry equivalent semantic identity internally before its first real reconstruction join: resource/config generation, frame identity, view/eye identity where applicable, camera/view identity, reset/history validity, extents/subrects, jitter and MV conversion metadata. The exact W40KRT ABI is Unity/D3D11-specific and should not be copied as a public ABI.

### Passive temporal probing before integration

**Observed from pinned source:** `WaaaghTemporalProbe.cs` first observes the engine's live TAA graph without GPU readback. It records eye, frame, epoch, camera and camera-buffer ownership, reversed-Z state, jitter matrix and descriptors for source, destination, velocity and depth-copy resources. Samples are bounded and spaced apart, and the observer disables itself on contract mismatch.

**Relevance:** this is a useful validation pattern for legacy adapters: establish resource identity, ownership, lifetime and temporal metadata with a bounded passive probe before retaining resources or inserting a reconstruction pass. It reinforces the current LTR Bridge rule to resolve uncertainties with probes before framework construction.

### Independent eye history

**Observed from pinned source:** `EyeAaHistory.cs` keeps separate pending/seeded state and counters for left and right eyes, validates camera and camera-buffer ownership before seeding history, refuses invalid/self-copy handles, and resets both TAA and neural history together when a discontinuity is requested.

**Relevance:** per-eye history is an ownership constraint, not only an `eye_id` label. Future LTR Bridge stereo work should reject camera/view ownership mismatches and make history reset propagate to every temporal consumer for that view.

### Bridge lifecycle patterns reusable outside Rogue Trader

**Observed from pinned source:** `src/native/core/Bridge.h` snapshots immutable frame data under a monotonically increasing serial, queues GPU work for a render-thread event, provides nonblocking/try-copy diagnostics, and only permits resize between complete frame pairs. Shutdown may report work still pending so the caller can continue pumping retirement rather than freeing resources early.

These are useful generic VR-mod patterns for the other repositories as well: immutable frame snapshots, explicit serial identity, render-thread GPU ownership, nonblocking diagnostics and deferred resource retirement. They do not depend on Rogue Trader's gameplay or tabletop presentation.

### Evidence boundary

**Observed:** the source contracts above and the release's stated DirectX 11/OpenXR/Quest 3 scope.

**Experiment-pending:** applying these semantics to the D3D9/x86 Call of Juarez path, any PSVR2/SteamVR compatibility claim for W40KRT_VR, and any performance/visual-quality conclusion from its beta. The beta release itself states that other headsets, storefronts and mod combinations have not yet been validated.

### Additional pinned files reviewed

Pinned to `Beren5556/W40KRT_VR@929bc9c1626b92d102def4ab405bf943c83bab24` / `v0.9.79-beta`:

- `README.md`
- `BUILDING.md`
- `THIRD_PARTY_NOTICES.md`
- `src/native/core/Bridge.h`
- `src/native/core/neural/Neural.h`
- `src/managed/RTMaquetaXR/EyeAaHistory.cs`
- `src/managed/RTMaquetaXR/WaaaghTemporalProbe.cs`

## 2026-09-30 recheck — W40KRT_VR 0.9.81

The public VR project has moved from the first `v0.9.79-beta` snapshot above to `v0.9.81-beta`, published 2026-09-24 at commit `0fb98a2f991075256dffd2117f9d458360caa324`. The earlier temporal-guide/history findings still apply, but two changes materially sharpen the contract relevant to LTR Bridge.

### Reconstruction configuration is versioned separately from frame identity

**Observed from pinned source:** the native neural ABI advanced from version 1 to version 2. `RTN_Config` now carries an explicit requested model/preset rather than expressing the old forced-K choice through a feature flag. The shipped configuration accepts automatic selection or the J/K/L/M model family, and `RTN_GetPresetStatus` reports the requested preset plus the identified preset for each eye when evidence is available.

**Implication:** reconstruction policy such as DLSS/DLAA model choice belongs in backend configuration state, not in the semantic frame payload. A configuration change should advance generation/reset state so delayed work cannot cross between backend configurations even when color/depth/MV resources are otherwise compatible.

### The OFXR join now validates backend completion, not only frame descriptors

**Observed from pinned source:** the optional OFXR host no longer relies on its previous small compatibility status. It queries the same `RTN_BackendStatus` exported by the shipped neural backend and permits neural-backed synthesis only when the backend is `READY`, reports no failure reason, matches the frame's neural generation, and reports both `lastLeftFrame` and `lastRightFrame` equal to the current game frame.

The OpenXR layer also distinguishes a structurally ineligible host frame from a neural frame that failed this compatibility check instead of collapsing both into one empty-mapping reason.

**Implication:** LTR Bridge should validate producer/consumer joins with explicit stage status in addition to resource metadata. For stereo reconstruction, a downstream presentation/synthesis stage should be able to reject a frame because its temporal descriptors are invalid, because reconstruction is not ready, or because generation/frame/view completion does not match; those failure classes should remain distinguishable in diagnostics.

### Broader runtime evidence, still scoped to that project

**Observed upstream:** `v0.9.81-beta` lists physically validated direct-OpenXR paths for Meta Quest, PICO 4/4 Ultra and Pimax Dream Air/Dream Air SE SLAM, while retaining the rule that unlisted headset/runtime combinations are outside the beta. This is stronger evidence that the project's renderer/reconstruction path can survive more than one OpenXR runtime family, but it is not evidence for PSVR2, SteamVR, legacy x86 transport or LTR Bridge compatibility.

The older `v0.9.79-beta` snapshot remains useful because it records the first public form of the temporal contracts. For current implementation details, use `Beren5556/W40KRT_VR@0fb98a2f991075256dffd2117f9d458360caa324` / `v0.9.81-beta`.
