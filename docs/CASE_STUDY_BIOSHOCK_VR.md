# Case study — BioShock VR DLSS/DLAA

Research date: 2026-09-13.

Upstream target:

- Repository: https://github.com/Beren5556/BioShock-VR-DLSS-DLAA
- Release: `v0.2.17-en`, published 2026-09-12.
- Release source commit: `8671fc87c4646140419ea64bd6e60d59fcac4723`.
- English release verification states that rendering/runtime behavior is byte-identical to the previously accepted Spanish `v0.2.16` payload; the newly compiled English binaries themselves did not receive a new physical-headset acceptance test.

This project is especially relevant to LTR Bridge because it is a real D3D11 x86 VR integration that separates game-specific temporal-data production from an x64 NVIDIA reconstruction host. It is not evidence that the same techniques work unchanged on D3D8, D3D9, D3D10, another engine, or another GPU backend.

## Architecture

**Verified from upstream source/documentation:** BioShock Remastered and the injected VR module run in the game's x86 process, while NVIDIA NGX executes in separate x64 helper processes. Each VR eye owns an independent helper, IPC channel, shared resources, synchronization and temporal history.

The documented flow is:

```text
BioShock x86 / D3D11
  -> game-specific temporal provider
  -> color + depth + motion
  -> x86 D3D11 client / shared-resource transport
  -> independent x64 host for each eye
  -> NGX DLAA or DLSS Super Resolution
  -> same eye's OpenXR swapchain
```

The x64 host is explicitly derived from DLSS5-Feeder and NIGos/dlss5-bridge. The project limits that host to DLSS 4.5 SR/DLAA and preserves the old `dlss5-*` filenames only for provenance.

**Architectural relevance:** this independently strengthens the same broad separation currently proposed by LTR Bridge:

`Legacy/Game Adapter -> Temporal Data Provider -> Transport -> Modern Host -> Reconstruction Backend -> VR output`

It also demonstrates why view identity belongs in the contract rather than being an implementation detail: mixing histories/resources between eyes is treated as a correctness failure.

## Temporal-provider separation

**Verified:** BioShock 1 and BioShock 2 do not share game camera addresses, matrices or hooks. They share backend-facing temporal types/resources but implement separate providers (`bioshock1r/temporal_guides.*` and `bioshock2r/temporal_guides.*`).

The shared `temporal_types.h` carries transport/diagnostic values rather than owning game-camera production. It records exact build/camera identity, depth identity, history validity, reset requirements, coherence and explicit rejection reasons such as missing/ambiguous camera or projection.

**Conclusion for LTR Bridge:** a reusable suite should share semantic types, validation and backend/transport mechanisms while keeping engine/game observation in profiles/providers. This is concrete real-world evidence for the same mechanism-vs-profile split already intended for LTR Bridge.

## Depth

**Verified for the BioShock 1 provider:** the D3D11 adapter observes candidate D24 depth-stencil resources, rejects incompatible formats/shapes, tracks draw/clear activity, preserves a selected depth resource, converts D24 to R32F hardware depth, and uses that depth for motion reconstruction.

**Verified for BioShock 2:** the provider restricts candidate depth to correctly sized, single-sample mip-0 D24 resources and uses render-activity votes. It explicitly states that this remains a heuristic rather than semantic identity of the engine's main depth object.

**Relevance:** this is a useful game-integrated counterpart to the generic ReShade depth research. It reinforces that depth discovery, capture, semantic convention and temporal identity are separate concerns and that a provider should expose why a depth resource was selected.

## Motion vectors

**Verified:** current BioShock temporal motion is reconstructed from depth plus the current/previous camera state. The convention is current pixel -> previous pixel in render-pixel units, stored as `R16G16_FLOAT` with scale 1,1.

**Verified limitation:** these are camera-only vectors. Independently moving or animated content — documented examples include hands, weapons, enemies, particles and water — does not receive object motion. The project explicitly warns that coherent camera/depth/IPC and successful NGX evaluation do not imply complete motion-vector quality.

**Conclusion for LTR Bridge:** this is a much stronger baseline than blind optical flow for rigid-world/head movement when real camera matrices are available, but it does not remove the need for renderer/object motion. LTR Bridge should add a depth+camera ground-truth-ish baseline to the controlled D3D11 harness and compare it against native/object-aware motion and image-space flow.

## Projection and jitter

**Verified for BioShock 2:** the adapter identifies the exact primary WORLD projection associated with the tagged eye/build and rejects secondary foreground, portal/reflection, duplicate, stale or mismatched projections. It captures actual near/far values rather than assuming configuration values.

**Verified limitation:** the renderer projection has no temporal jitter. The client sends `jitterX=0`, `jitterY=0` for both DLAA and SR. Upstream explicitly states that missing jitter limits temporal information, especially for SR, and does not claim equivalence to a complete renderer-native integration.

**Conclusion:** BioShock demonstrates that real NGX Super Resolution can execute with lower render resolution, depth and camera-only vectors even when renderer jitter/object motion are incomplete. That is evidence of a functioning partial temporal contract, not evidence that jitter or object vectors are optional for high-quality/general SR. LTR Bridge should preserve provenance/completeness metadata rather than reducing success to "NGX accepted the frame".

## History and rejection

**Verified:** each eye owns independent history. BioShock 2 resets/rejects history for projection/plane changes, sequence gaps, camera epochs, stale/nonmonotonic camera data, large camera cuts, resource recreation and ambiguous/missing camera/depth/projection data.

This is particularly reusable. LTR Bridge's provisional contract already includes reset semantics, but the BioShock implementation provides concrete rejection cases and demonstrates the value of an exact frame/build identity rather than a "latest camera" fallback.

## x86 -> x64 stereo transport test

**Verified:** upstream contains a game-independent synthetic `bvr-stereo-client32` test. It exercises a 32-bit D3D11 client, two independent x64 hosts, separate pipes/resources/fences/histories, 300 frames per eye, DLAA and SR, and readback patterns designed to detect cross-eye contamination. A second runtime test uses the real `dlss45_client.cpp` and frozen x64 host/runtime without starting BioShock or OpenXR.

**Conclusion for LTR Bridge:** this is directly relevant to the planned D3D11 x86 -> x64 probe. The useful idea is not to copy BioShock's IPC ABI, but to add stereo contamination checks, independent eye histories, deterministic readback, transition/recovery tests and bounded helper shutdown to our transport probe from the beginning.

## Validation level

**Observed upstream:** BioShock 2 documentation records real NGX execution for DLAA and multiple SR resolutions using two per-eye hosts and an OpenXR simulator, including thousands of processed frames and explicit coherence/history diagnostics. It also records known teardown access violations rather than treating process exit code 0 as proof of a clean shutdown.

**Important qualification:** the BioShock 2 temporal document calls this a technical-port validation rather than final physical-headset quality. The English `v0.2.17-en` release likewise states that the rebuilt English binaries did not receive a new physical-headset acceptance test. Simulator imagery proves output/stereo flow, not absence of ghosting, headset comfort, sustained target refresh rate or complete object motion.

LTR Bridge should therefore classify these findings as upstream **observed** evidence, not `vr-headset-validated` evidence for LTR Bridge itself.

## Performance ideas worth studying later

The English release documents four stable optimizations: left-eye overlap, depth-copy reuse, tail overlap and early XR delivery. These are interesting after correctness is established because a two-eye temporal backend can otherwise serialize expensive work. They should not be imported into the first LTR Bridge probe before the basic resource/history contract is validated.

## Licensing/provenance

**Verified:** the BioShock VR repository is MIT. Its x64 host documentation preserves attribution to DLSS5-Feeder and NIGos/dlss5-bridge, while NVIDIA NGX SDK/runtime material remains subject to NVIDIA's separate terms. SDK headers/libraries are not stored in the repository.

Reuse of MIT implementation ideas/source is therefore possible with the required attribution/license notice, but proprietary NVIDIA components remain a separate packaging/licensing decision.

## What this changes for LTR Bridge

1. **Keep the current layered architecture.** This project is strong independent evidence that game-specific temporal providers can sit above a reusable transport/backend layer.
2. **Strengthen the D3D11 harness before legacy API work.** Add exact frame/build identity, explicit rejection reasons, per-eye-ready semantic types, camera+depth motion reconstruction and history reset diagnostics.
3. **Strengthen the x86/x64 probe.** Test two independent eye streams, cross-eye contamination, deterministic GPU output/readback, helper loss/recovery and bounded shutdown rather than only a single flat stream.
4. **Do not weaken the jitter/object-MV requirements.** BioShock works with zero jitter and camera-only vectors, but upstream itself treats those as quality limitations. LTR Bridge should model temporal completeness/provenance explicitly.
5. **Do not skip D3D9/D3D10/D3D8 experiments.** BioShock Remastered is D3D11; this case study provides no evidence that older API transport/depth interception behaves the same way.
6. **Use BioShock as a reference implementation, not a mandatory dependency.** The most reusable value is its separation, tests, validation/rejection discipline and per-eye model. Game addresses, projection hooks and camera logic are intentionally profile-specific.

## Primary upstream files reviewed

Pinned to commit `8671fc87c4646140419ea64bd6e60d59fcac4723`:

- `docs/DLSS-DLAA-ARCHITECTURE.md`
- `docs/BS2-TEMPORAL.md`
- `docs/ENGLISH-0.2.17.md`
- `src/game/shared/temporal_types.h`
- `src/game/bioshock1r/temporal_guides.cpp`
- `src/core/gfx/dlss45_client.cpp`
- `components/dlss-host/README.md`
- `components/dlss-host/tests/README-bvr-stereo.md`
- `LICENSE`
