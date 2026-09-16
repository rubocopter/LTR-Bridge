# x86 -> x64 bridge research

Status: **local D3D11 x86 -> D3D12 x64 multiframe prototype implemented and host-tested; repeated frames, true mid-run resource replacement, controlled process/device loss, bounded ring-depth backpressure, two-eye isolation, synthetic per-eye GPU-history isolation/reset semantics, high-resolution copy, 4x-MSAA resolve, R10-to-RGBA8 conversion, a small format/interoperability matrix, negative-path diagnostics, and a current-host D3D9Ex render-target -> relay -> bridge route are covered; broader real-renderer integration, failure timing and OpenXR/headset execution remain pending**.

## Why a bridge is needed

Many legacy Windows games are 32-bit. A 32-bit process cannot load 64-bit-only components. Current community work demonstrates a practical alternative: keep renderer interception in x86, move modern evaluation into an x64 helper, and share GPU resources between the processes.

## Demonstrated D3D11 pattern

DLSS5-Feeder currently demonstrates this broad sequence:

```text
x86 D3D11 game/add-on
  create/open shareable GPU textures
  <control IPC + duplicated handles>
x64 helper
  D3D12 device on same adapter
  open shared resources/fences
  execute modern work
  signal completion
x86 client
  GPU wait/copy/blit result into game output
```

The useful architectural lesson is independent of DLSS 5: bitness does not force pixel data through system memory when the graphics APIs and driver permit cross-process GPU resource sharing.

## D3D11/D3D12 primitives

Microsoft documents a direct interop family based on:

- D3D11 resources created with NT-handle sharing (`D3D11_RESOURCE_MISC_SHARED_NTHANDLE` where applicable);
- `ID3D11Device::OpenSharedResource1`;
- D3D11 shared fences through `ID3D11Device5::OpenSharedFence`;
- D3D12 `CreateSharedHandle` and `OpenSharedHandle` for resources/fences.

Process-handle duplication is control-plane plumbing. The resource remains GPU memory; the handle itself is not the data.

## Resource sharing is not automatically zero-copy

For measurement, count these separately:

1. renderer -> staging/shared input GPU copy;
2. shared resource visibility across devices/processes;
3. backend-private working copies;
4. backend output -> shared output GPU copy;
5. shared output -> renderer target GPU copy/blit;
6. CPU readback/upload, if any.

A design with no CPU pixel transfer can still perform several GPU copies per frame.

## Proposed transport protocol properties

The first local bridge probe should be backend-neutral and include:

- protocol magic + explicit version;
- source API and process bitness;
- adapter identity (LUID where applicable);
- frame/session generation number;
- resource descriptors: role, dimensions, format, color space, sample count;
- resource ownership direction (client-created vs host-created);
- shared synchronization handles;
- bounded frame sequence numbers;
- resize/rebuild message;
- clean shutdown and host-loss detection;
- diagnostics for every capability fallback.

Pixel data must not travel over the control pipe.

## D3D10 boundary

D3D10 cannot simply reuse the D3D11 NT-handle/fence route. Current Feeder demonstrates an in-process private D3D11 relay:

```text
D3D10 game device
    | legacy shared texture / copy
    v
private D3D11 relay device (same adapter)
    | D3D11 modern sharing/fence path
    v
x64 D3D12 helper
```

This is an important candidate pattern, but local work must measure the additional copies and synchronization cost. It also needs testing across D3D10.0/10.1 devices and multiple drivers.

Microsoft documents D3D10.1/DXGI keyed-mutex synchronization, while Feeder reports real-hardware failure to create keyed-mutex resources in its tested path and therefore uses event-query synchronization. LTR Bridge should probe the capability and log the observed result instead of assuming either behavior universally.

## D3D9 boundary

Native D3D9 resources are not a drop-in D3D12 transport object, but Microsoft documents a narrower direct D3D9 -> D3D11 texture-sharing path through `ID3D11Device::OpenSharedResource`. The D3D9 texture is created with `pSharedHandle`, then opened by a D3D11 device.

That path is heavily constrained: 2D, one mip, default usage, no MSAA, and only a small format set (`R10G10B10A2_UNORM`, `R16G16B16A16_FLOAT`, `R8G8B8A8_UNORM`). It therefore looks more like a purpose-built relay surface than a way to expose arbitrary native game resources.

Microsoft's broader interoperability documentation also says unsynchronized sharing is supported by D3D9Ex while D3D9c and older runtimes do not support shared surfaces. The local Win32 probe now agrees with that narrower reading on the current host: classic D3D9 returns `D3DERR_INVALIDCALL` for every tested shared-texture creation, while D3D9Ex successfully exports R10G10B10A2 and RGBA16F resources that D3D11 opens as `SRV|RTV`. The result has been repeated five times with resource recreation and zero pixel mismatches, but remains host/driver-scoped evidence rather than a universal Windows rule.

The D3D9Ex probe also found a format nuance that matters for a practical relay: documented `A8B8G8R8 -> R8G8B8A8_UNORM` shared creation fails on this host, whereas an `A8R8G8B8 -> B8G8R8A8_UNORM` control outside the documented interop list succeeds. The portable relay contract therefore should stay on the documented R10/RGBA16F paths until more hardware is tested; BGRA8 can be treated only as an optional capability discovered at runtime.

Candidate routes:

1. D3D9Ex adapter -> dedicated relay textures -> private D3D11 device -> modern sharing; this is now the leading native route on the current host;
2. classic D3D9 adapter -> another explicit transport if direct sharing is unavailable;
3. D3D9 -> dgVoodoo2 -> D3D11 -> modern sharing.

That D3D9Ex gate is now host-tested through 1440p. The x86 producer creates a local D3D9Ex `A2B10G10R10` render target, binds and clears it, restores the default target, copies it with `StretchRect` into a shareable R10 relay, waits for `D3DQUERYTYPE_EVENT`, opens the relay in a private D3D11 device, and converts it by fullscreen shader into the existing host-created shared RGBA8 transport resource. The x64 D3D12 consumer then follows the established fence protocol and compute path. Five 24-frame repetitions pass for `64x64 -> 96x72` and five for `1920x1080 -> 2560x1440`; each run includes one `ResetEx`/resource replacement and zero mismatches. Validation allows ±1 8-bit LSB on the R10 source because D3D9 render-target quantization can differ slightly from the synthetic reference. At high resolution, run-mean D3D9Ex bind/clear/restore/`StretchRect`/event CPU-wall time averaged about `0.3637 ms`, while run-mean D3D11 R10-to-RGBA8 shader time averaged about `19.5638 us`. These are controlled single-host measurements, not performance validation.

The bridge now also has a controlled D3D9Ex scene profile. The x86 side owns a default-pool vertex buffer, R10 color target and D24S8 depth surface, applies per-frame world transforms, renders overlapping depth-tested geometry, and captures the engine target with `StretchRect` while both color and depth remain bound. It verifies the current render target, depth surface and world transform before capture, then restores the default target after the event-query completion. Five `640x360 -> 1280x720` 24-frame runs completed one `ResetEx`/resource transition with zero mismatches through the x64 D3D12 transform. Across those runs the D3D9Ex scene-draw/capture CPU-wall run mean averaged about `0.2881 ms`, and the private D3D11 R10-to-RGBA8 shader run mean about `5.0509 us`. The validation samples clear, red-only, red-over-green depth overlap and green-only regions with ±2 RGB8 LSB tolerance. This establishes a controlled engine-style resource/state path, not an external game interception or performance result.

## Host-created vs client-created resources

The protocol should allow either direction. Upstream evidence shows this matters when:

- client API cannot export a resource type the host can open;
- client feature level cannot create the required bind/UAV combination;
- another API supports importing modern external memory more naturally than exporting it.

## Synchronization goals

The bridge should avoid CPU blocking on every frame. Candidate steady-state flow is GPU signal/wait plus lightweight control sequencing. The probe must capture:

- CPU wait time;
- GPU queue wait time;
- overlap between game and host work;
- queue starvation;
- ring depth and backpressure;
- what happens when host evaluation exceeds the game frame interval.

## Failure and lifecycle cases

The first prototype is incomplete unless it deliberately tests:

- host crash/termination;
- client exit;
- window resize;
- fullscreen/windowed transition;
- renderer device reset/loss;
- backend feature recreation;
- adapter mismatch;
- unsupported texture format;
- MSAA input;
- stale/mismatched protocol versions.

## Experiment 2 success criteria

The bridge can be promoted from `implemented` to `host-tested` only when a deterministic texture pattern makes this round trip:

`x86 D3D11 -> shared GPU resource -> x64 D3D12 transform -> shared output -> x86 D3D11`

with pixel verification, no CPU pixel copy, repeatable resize/rebuild, clean teardown, and recorded timing/copy counts.

## Local round-trip probe — 2026-09-16

The first local bridge probe is implemented under `src/x86_x64_bridge_probe/` and driven by `tools/run_x86_x64_bridge_probe.ps1`. It builds a Win32 D3D11 client and an x64 D3D12 host independently, then exercises two sequential shared `R8G8B8A8_UNORM` resource generations on the same adapter with separate unidirectional shared fences.

The working ownership direction is host-created:

```text
x64 D3D12 host
  create shared committed texture + NT handle
  launch x86 client with generation-0 inherited resource handle
        |
        v
x86 D3D11 client
  OpenSharedResource1
  upload deterministic pattern
  create named shared D3D11 fence
  signal producer-ready
        |
        v
x64 D3D12 host
  open the D3D11 fence
  GPU-wait producer-ready
  invert RGB in-place with a compute shader
  GPU-signal consumer-done
        |
        v
x86 D3D11 client
  GPU-wait consumer-done
  copy to staging only for validation readback
  verify all 4096 pixels
```

**Host-tested:** the original one-frame local run completed with `mismatches=0`, both processes reported `RESULT PASS`, and no pixel payload crossed the process boundary. The only CPU pixel transfer is the D3D11 staging readback used to verify the experiment.

### Multiframe and dynamic generation replacement

The probe now runs 12 frames at `64x64`, then 12 frames at `96x72`, with a frame-varying deterministic payload. Only generation 0 exists when the x86 process launches. After frame 12 completes on the D3D12 `done` fence, the x64 host releases its generation-0 resource, creates generation 1, duplicates the new NT handle directly into the already-running x86 process with `DuplicateHandle`, and sends a compact control record through an inherited anonymous pipe. The record carries a magic value, protocol version, generation number, extent and target-process handle value. The x86 producer has already completed validation and releases its generation-0 references before reading that record; it validates the control contract, opens the new resource, creates its new staging texture and resumes with monotonically increasing fence values. This is a true mid-run resource replacement probe, not a pre-created generation switch.

**Host-tested:** the dynamic-replacement probe passed the full positive/negative driver and five additional positive stability runs. Each positive run completed `24` frames, one live `64x64 -> 96x72` replacement and `0` mismatches. Across the five stability runs, the per-run mean D3D12 compute interval averaged `4.0874 us`, with per-run means ranging `3.925–4.352 us`. The per-run mean x86 signal-to-validation-readback wall time averaged `1.9595 ms`, with per-run means ranging `1.6301–2.1445 ms`; that path includes synchronization, the validation copy and CPU readback and is not a transport-only latency measurement. These remain short single-host controlled measurements and are not `performance-validated`.

The multiframe probe also records copy scope explicitly: the x64 D3D12 transform performs zero transport copies because it operates in-place on the shared resource; the x86 test performs one GPU copy to staging per frame solely for deterministic validation. The baseline producer's synthetic `UpdateSubresource` input is test-data upload and is not evidence for the copy count of a future renderer adapter.

### Renderer-local -> shared-resource transfer probes

**Implemented/host-tested:** a separate renderer-copy mode adds the missing input-copy step without changing the cross-process protocol. For each frame, x86 uploads the deterministic test payload into a non-shared D3D11 default texture, records D3D11 GPU timestamps immediately around one `CopyResource` from that local texture into the host-owned shared transport resource, then signals the normal `ready` fence. The x64 host still transforms the shared resource in place, so this mode has exactly one renderer-to-shared GPU copy per frame plus the existing validation-only shared-to-staging copy after host completion.

The full driver passed with `24` renderer-to-shared copies, the live `64x64 -> 96x72` resource replacement and `0` pixel mismatches. Five additional renderer-copy runs also passed. Their per-run GPU-copy means were `2.5373`, `2.5640`, `2.5987`, `2.5947` and `2.7507 us`, averaging `2.6091 us` with a `2.5373–2.7507 us` range between run means. The timestamp interval excludes the preceding synthetic `UpdateSubresource` and the later validation readback.

The same transport now has three larger renderer-input profiles, all with the same live generation replacement and deterministic end-to-end pixel validation:

- **High-resolution `CopyResource`:** `1920x1080 -> 3840x2160`, local and shared textures both `R8G8B8A8_UNORM`, one same-size copy per frame. Five repeated runs completed with `0` mismatches. The per-generation copy mean averaged `23.9184 us` at 1080p (`22.4560–25.9973 us` between run means) and `93.9013 us` at 4K (`90.2480–96.6720 us`).
- **4x-MSAA resolve:** `1920x1080 -> 2560x1440`, with a local `R8G8B8A8_UNORM` 4x-MSAA render target resolved into the single-sample shared resource using `ResolveSubresource`. The deterministic payload is first rendered into the MSAA target; the timestamp covers only the resolve into the bridge resource. Five repeated runs completed with `0` mismatches. Resolve means averaged `15.0016 us` at 1080p (`14.8480–15.1893 us`) and `21.1627 us` at 1440p (`20.9067–21.5893 us`).
- **R10 -> RGBA8 conversion:** `1920x1080 -> 2560x1440`, with a local `R10G10B10A2_UNORM` renderer texture and a fullscreen D3D11 shader pass into the shared `R8G8B8A8_UNORM` resource. Five repeated runs completed with `0` mismatches. Shader-transfer means averaged `29.1685 us` at 1080p (`27.2000–30.5093 us`) and `46.3008 us` at 1440p (`43.6507–49.0693 us`).

These are **host-tested** controlled transfer measurements, not `performance-validated` renderer costs. Synthetic upload is outside every reported transfer interval; the MSAA number excludes the draw that fills the multisampled target. The probes establish that this host can feed the fixed RGBA8 bridge contract from a large same-format render target, a 4x-MSAA target and an R10 renderer target without pixel mismatches. Real game render-target hazards, scaling, multiple simultaneous attachments, two full-resolution eyes, engine scheduling and multi-host/GPU behavior remain pending.

**Verified synchronization refinement:** the one-frame probe could alternate `D3D11 Signal -> D3D12 Signal` on one shared fence, but that design failed when reused for multiple frames: the next D3D11 `Signal` returned `E_INVALIDARG`. The passing multiframe design therefore uses two unidirectional fences: a D3D11-created `ready` fence signaled only by x86 and a D3D12-created `done` fence signaled only by x64. D3D11 can open and wait on the D3D12-created fence even though the earlier attempt to signal that fence from D3D11 failed.

**Host-tested negative paths:** protocol-version mismatch is rejected explicitly; a deliberately invalid adapter LUID fails with an adapter diagnostic; a mismatched generation-0 extent is rejected before frame execution; a host-stall probe detects that the `done` fence does not advance and exits after a controlled `250 ms` timeout; and a malformed dynamic replacement carrying the wrong generation marker is rejected after generation 0 completes. The probe also now exercises real process/device loss at the generation boundary: abrupt x86 termination is observed and cleaned up by the host; `ID3D12Device5::RemoveDevice()` produces `DXGI_ERROR_DEVICE_REMOVED` and the x86 exits when the control channel closes; and an abrupt self-termination of the x64 host is detected by the x86 through an inherited `SYNCHRONIZE` process handle.

**Host-tested format-contract matrix:** the x64 host now deliberately creates three generation-0 resources whose formats do not match the producer contract. `R16G16B16A16_FLOAT` (`DXGI_FORMAT=10`) and `R8G8B8A8_UINT` (`DXGI_FORMAT=30`) both cross the D3D12 -> D3D11 shared-resource boundary successfully and are then rejected by the x86 descriptor check before any frame executes. `R32G32B32A32_FLOAT` is rejected one layer earlier on this host: D3D11 `OpenSharedResource1` returns `E_INVALIDARG` (`0x80070057`), so the producer never obtains a texture descriptor. The complete driver passed, and five additional repetitions of each format reproduced the same rejection point and exit code. This is useful scoped evidence that “unsupported format” can mean either an interop-open failure or an application-contract rejection; it is not a general DXGI-format support table.

**Observed host-loss detail:** when the x64 host is terminated, the shared-fence completion event can wake before the host process object becomes signaled. The negative probe therefore treats fence wake-up as ambiguous and performs a bounded `250 ms` liveness check on the host process handle before accepting completion. This is scoped host evidence, but it is enough to reject a design that infers host health from fence state alone.

### Bounded backpressure probe

**Implemented/host-tested:** a separate fixed-size backpressure mode now runs `24` `64x64` frames with pre-created rings of depth `1` and `2`. This mode deliberately does not exercise dynamic replacement: its purpose is to isolate slot reuse and queue pressure. The x86 producer may submit at most `depth` frames without validation; before a slot is reused it waits for that slot's prior frame to reach the shared D3D12 `done` fence, copies the completed result to staging, validates it, and only then overwrites the slot. The x64 host inserts a one-time `50 ms` startup delay to force the producer to fill the ring and encounter real reuse pressure.

**Host-tested:** five additional runs at each depth all completed with `0` mismatches and the expected bound: `max_in_flight=1` for depth 1 and `max_in_flight=2` for depth 2. Depth 1 recorded all `23/23` possible reuse checks as pending in every run. Depth 2 recorded `16–21` pending reuse waits out of `22`, averaging `18.2`. The per-run mean x86 wait+validation-readback interval averaged `4.1463 ms` at depth 1 (`4.0331–4.2882 ms`) and `3.8202 ms` at depth 2 (`3.5088–4.5201 ms`). Per-run mean D3D12 compute intervals averaged `4.2324 us` (`4.053–4.352 us`) and `3.9938 us` (`3.200–4.907 us`) respectively. These short synthetic single-host values demonstrate bounded reuse and pressure handling; they are not `performance-validated` and do not establish that depth 2 is universally faster.

**Host-tested in-flight host-loss negative:** a depth-1 negative now waits until the x86 producer has uploaded frame 0 and signaled `ready=1`, then terminates the x64 host before it signals the matching `done=1`. On the producer's first slot-reuse validation it waits on both the `done` fence event and the inherited host process handle rather than entering the normal D3D11 queue wait blindly. The full driver and five additional repetitions all reported `reject=backpressure_host_terminated detected_by=process_handle frame=0 slot=0 target=1` and exited cleanly. In every repetition `fence_wait_result=0`: the fence event became signaled first during teardown, but the bounded `250 ms` process-handle check then confirmed host death. This extends the earlier host-loss observation into a real bounded-queue reuse point and reinforces that fence wake-up alone is not host-liveness evidence.

This negative is intentionally narrow. It establishes recovery from host loss while the producer is awaiting the first depth-1 slot reuse; it does not yet cover every point inside D3D12 command recording/execution, depth-2 failure placement, device removal during a queued dispatch, or recovery/restart semantics.

### Controlled stereo transport probe

**Implemented/host-tested:** a separate stereo mode creates two host-owned `64x64 R8G8B8A8_UNORM` resources before launch, one per eye. The x86 side opens both resources and creates two independent D3D11 `ready` fences; the x64 side supplies two independent D3D12 `done` fences. Each eye therefore has its own resource and synchronization timeline. The probe runs `12` temporal frames per eye (`24` view dispatches) and keeps CPU readback validation-only.

Each eye receives a distinct deterministic RGB sequence and a persistent alpha identity marker (`0x4c` left, `0xd3` right). Validation checks the expected transformed RGB and the eye marker for every pixel. `cross_eye_contamination` is incremented whenever an eye receives the other eye's marker, so eye identity is checked independently of RGB correctness.

**Host-tested:** the full driver passed, then five additional stereo runs completed with `0` mismatches and `0` cross-eye contamination. Across those five runs, per-run mean D3D12 compute intervals averaged `4.207 us` (`4.139–4.267 us`) and per-run mean x86 wait+validation-readback intervals averaged `2.2509 ms` (`2.0720–2.4701 ms`). These are short single-host synthetic measurements and are not `performance-validated` VR throughput evidence.

**Host-tested negative path:** a synthetic contamination mode deliberately forces the right-eye alpha marker into the transformed left-eye stream. The detector reported exactly `49,152` contaminated pixels (`64 * 64 * 12`) and the producer rejected the run. This validates the contamination check itself; it does not emulate every real cross-eye resource/history bug.

This probe establishes two-eye transport isolation only. The current x86 validation loop submits and validates the two eyes serially, so it does not measure useful stereo overlap or headset frame pacing. It also does not provide OpenXR swapchains/presentation, pose/FOV timing, frame interpolation or physical-headset validation.

### Controlled per-eye GPU history probe

**Implemented/host-tested:** a stereo-history mode adds one D3D12 UAV history resource per eye while preserving the two independent eye transport resources and ready/done fence timelines. Temporal frame 0 is an explicit history reset: the compute pass transforms the current eye payload and writes that result into the matching eye history. For temporal frames 1-11, the source payload continues changing, but output is loaded from that eye's prior GPU history and written back to the same history resource. The x86 validator therefore expects every later output to remain equal to the transformed frame-0 payload for that eye.

The full bridge driver passed with `12` temporal frames per eye (`24` view dispatches), `0` mismatches and `0` cross-eye contamination. Five additional positive runs also passed with zero mismatches/contamination. Across those five runs the per-run D3D12 compute mean averaged `3.3108 us` (`3.200-3.669 us`) and the x86 wait+validation-readback mean averaged `1.9954 ms` (`1.6870-2.1687 ms`). These are short synthetic single-host measurements and are not `performance-validated`.

**Host-tested negative path:** after frame 0, a deliberate mode swaps the two history UAV bindings while leaving the eye transport resources unchanged. The existing per-eye identity validation rejected all `90,112` subsequent view pixels (`64 * 64 * 11 * 2`) as cross-eye contamination and the producer exited with the expected code `25`. Both histories are transitioned to UAV state during their frame-0 initialization and remain in UAV state for the rest of this controlled run, including the deliberate swapped bindings.

This establishes independent synthetic GPU-history identity and explicit reset semantics for each eye. It is not a reconstruction backend, real temporal upscaling, concurrent eye processing, OpenXR presentation/pacing, frame generation or headset validation. The history resources currently use the probe's shared-capable texture helper even though they remain host-local and are never exported; that is sufficient for this minimal experiment and is not an architectural requirement.

**Verified for this probe:** the x64 host can create a D3D12 committed resource with `D3D12_HEAP_FLAG_SHARED`, `ALLOW_RENDER_TARGET`, `ALLOW_UNORDERED_ACCESS`, and `ALLOW_SIMULTANEOUS_ACCESS`; the x86 D3D11 device on the same adapter can open that NT handle with `OpenSharedResource1`. Shared fences can cross the D3D11/D3D12 boundary, but the multiframe evidence requires the unidirectional ownership/signaling split described above.

**Observed during development:** two alternative paths failed on this host and should not be generalized from this single experiment. A D3D11-created shared texture was openable from D3D12, but the attempted D3D12 write path did not become visible to the D3D11 validation readback. A D3D12-created shared fence was openable from D3D11, but `ID3D11DeviceContext4::Signal` returned `E_INVALIDARG`. The passing probe therefore uses D3D12 ownership for the texture and D3D11 ownership for the fence. These failures are implementation evidence for ownership-direction testing, not proof that the opposite directions are universally unsupported.

The deterministic multiframe transport, one real mid-run resource replacement, renderer-to-shared transfer accounting at small/1080p/1440p/4K extents, 4x-MSAA resolve, R10-to-RGBA8 conversion, dynamic-control rejection, a small format/interoperability matrix, abrupt client termination, abrupt host-loss detection at startup and during depth-1 slot reuse, controlled D3D12 device removal, bounded ring-depth backpressure, controlled two-eye transport isolation and synthetic per-eye GPU-history isolation/reset semantics are now covered. Real-renderer hazards/scaling, broader format/sample-count combinations, mid-dispatch/device-loss timing, real reconstruction-backend histories, OpenXR presentation and headset execution remain experiment-pending.
