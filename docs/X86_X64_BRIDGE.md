# x86 -> x64 bridge research

Status: **local D3D11 x86 -> D3D12 x64 round-trip prototype implemented and host-tested; lifecycle, resize, timing, failure injection and stereo still pending**.

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

Microsoft's broader interoperability documentation also says unsynchronized sharing is supported by D3D9Ex while D3D9c and older runtimes do not support shared surfaces. The apparent mismatch with the `OpenSharedResource` page is an explicit experiment target; LTR Bridge must not generalize the route to classic D3D9 before testing it.

Candidate routes:

1. classic D3D9/D3D9Ex adapter -> dedicated relay textures -> private D3D11 device -> modern sharing, if the runtime/driver permits it;
2. direct D3D9 adapter -> another explicit transport proven by a focused spike;
3. D3D9 -> dgVoodoo2 -> D3D11 -> modern sharing.

No route is selected yet.

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

The first local bridge probe is implemented under `src/x86_x64_bridge_probe/` and driven by `tools/run_x86_x64_bridge_probe.ps1`. It builds a Win32 D3D11 client and an x64 D3D12 host independently, then exercises one shared `64x64 R8G8B8A8_UNORM` texture and one shared GPU fence on the same adapter.

The working ownership direction is host-created:

```text
x64 D3D12 host
  create shared committed texture + NT handle
  launch x86 client with inherited resource handle
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

**Host-tested:** the local run completed with `mismatches=0`, both processes reported `RESULT PASS`, and no pixel payload crossed the process boundary. The only CPU pixel transfer is the final D3D11 staging readback used to verify the experiment.

**Verified for this probe:** the x64 host can create a D3D12 committed resource with `D3D12_HEAP_FLAG_SHARED`, `ALLOW_RENDER_TARGET`, `ALLOW_UNORDERED_ACCESS`, and `ALLOW_SIMULTANEOUS_ACCESS`; the x86 D3D11 device on the same adapter can open that NT handle with `OpenSharedResource1`. A D3D11-created shared fence can be opened by D3D12 and used for GPU-side wait/signal ordering in both directions.

**Observed during development:** two alternative paths failed on this host and should not be generalized from this single experiment. A D3D11-created shared texture was openable from D3D12, but the attempted D3D12 write path did not become visible to the D3D11 validation readback. A D3D12-created shared fence was openable from D3D11, but `ID3D11DeviceContext4::Signal` returned `E_INVALIDARG`. The passing probe therefore uses D3D12 ownership for the texture and D3D11 ownership for the fence. These failures are implementation evidence for ownership-direction testing, not proof that the opposite directions are universally unsupported.

This closes only the minimal deterministic round trip. Resize/rebuild, repeated frames, timing, copy/stall accounting, host termination, adapter mismatch, unsupported formats, protocol versioning, backpressure and stereo remain experiment-pending.
