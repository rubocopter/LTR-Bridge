# x86 -> x64 bridge research

Status: **architecture observed upstream; local prototype pending**.

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

D3D9 supports shared resources on modern Windows under specific restrictions, but Microsoft notes that a shared resource must be opened through a matching API. This means a native D3D9 shared handle is not a drop-in D3D12 transport object.

Candidate routes:

1. D3D9 -> private relay API/device -> modern sharing;
2. D3D9 -> dgVoodoo2 -> D3D11 -> modern sharing;
3. another explicit copy/interop mechanism proven by a focused spike.

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
