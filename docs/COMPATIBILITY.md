# Evidence-based compatibility matrix

Snapshot: 2026-09-16. This is a research matrix, not a product support table.

States: `unknown`, `observed-upstream`, `planned`, `implemented`, `host-tested`, `live-tested`, `visual-validated`, `performance-validated`, `vr-headset-validated`, `supported`.

| Source API / architecture | Source/API integration | Translation/relay | x86 -> x64 helper | Native-res temporal AA | Real temporal SR | VR | Evidence |
| --- | --- | --- | --- | --- | --- | --- | --- |
| D3D11 x64 controlled harness | controlled source implemented | not required | not required | host-tested | unknown | unknown | Local harness is implemented, host-tested and visual-validated for controlled temporal cases. The separate D3D12 XeSS 3.0.2 probe executes Native AA at 1.0x with coherent jitter/MV semantics; this is still synthetic evidence, not a game integration. |
| D3D11 x86 -> D3D12 x64 host | controlled x86 producer host-tested; real-game interception pending | no translation expected | host-tested | unknown | unknown | host-tested synthetic stereo transport | Local bridge validates repeated frames, live resource replacement, bounded backpressure, failure paths, renderer-to-shared transfers through 4K, per-eye isolation and independent synthetic per-eye histories. No reconstruction backend or OpenXR presentation is connected to this transport yet. |
| D3D10 x86 -> D3D11 relay -> x64 host | controlled relay host-tested; real-game interception pending | host-tested | host-tested | unknown | unknown | unknown | Win32 D3D10.1/FL10.0 R10 source -> legacy shared R10 -> private D3D11 -> shared RGBA8 -> x64 D3D12 passes five repeated 24-frame `640x360 -> 1280x720` runs with zero mismatches. Event-query synchronization works; keyed-mutex resource creation returns `E_INVALIDARG` on this RTX 4070 Ti host. Depth/MV/SM4 temporal integration remains pending. |
| D3D10 direct to modern host | unknown | n/a | unknown | unknown | unknown | unknown | D3D10 lacks the D3D11 NT-handle/fence path used by current bridge designs. |
| D3D9 classic -> D3D11 relay | controlled API probe host-tested negative | current-host shared creation rejected | unknown | unknown | unknown | unknown | Win32 probe: all tested `CreateTexture(..., pSharedHandle)` cases return `D3DERR_INVALIDCALL` on the current RTX 4070 Ti host; five repetitions stable. Do not generalize beyond this host/runtime. |
| D3D9Ex -> D3D11 relay -> x86/x64 D3D12 host | controlled external-process interception host-tested; real-game integration pending | current-host R10 relay works end to end | host-tested | unknown | unknown | unknown | The controlled scene is validated standalone, through the x86/x64 bridge, and through a separate interceptor DLL that hooks API/COM entry points in an external target process. Five interception repetitions preserve bound color/depth/world state, survive `640x360 -> 1280x720` reset/recreation and finish with zero mismatches. Cooperative loading and the synthetic target are not a production injector or game result. |
| D3D9 -> dgVoodoo2 -> D3D11 | planned comparison | observed-upstream | observed-upstream downstream | observed-upstream in Feeder-style stack | not demonstrated as real SR | unknown | Existing community tooling uses this route. |
| D3D8 native | unknown | planned | unknown | unknown | unknown | unknown | Requires D3D8-specific investigation. |
| D3D8 -> dgVoodoo2 -> D3D11 | planned | observed in ecosystem | possible downstream | unknown | unknown | unknown | Deployment tools use this route; LTR Bridge has not validated it. |
| Stereo/OpenXR controlled harness | n/a | n/a | host-tested transport | unknown | unknown | OpenXR bootstrap host-tested | Two-eye transport/history isolation is host-tested. A separate OpenXR bootstrap negotiates with SteamVR and confirms D3D11/D3D12 extensions, but the current runs had no HMD system, so graphics-session, swapchain, pacing, presentation and headset execution remain pending. |

## Backend/API observations

| Backend family | Current useful API evidence | Legacy relevance |
| --- | --- | --- |
| NVIDIA DLSS/DLAA via Streamline/NGX | Streamline currently targets 64-bit Windows and modern D3D11/D3D12/Vulkan-era integration with explicit temporal inputs | Strong reason for an x64 modern host when the source game is x86; still requires correct legacy data extraction. |
| FidelityFX temporal upscaling | Current FSR SDK is 2.3.0; the current FSR API is delivered through signed DX12 DLLs. The exact v2.3.0 loader/upscaler and the other signed DX12 DLLs were verified as PE x64 (`0x8664`). | Useful x64 D3D12-host backend candidate after a legacy provider exists. The v2.3.0 signed runtime cannot be loaded directly into an x86 process; future releases must be rechecked rather than assumed identical. |
| XeSS-SR / Native AA | XeSS SDK 3.0.2 requires Windows x64; D3D12 is cross-vendor, while its documented D3D11 SR path is limited to Intel Arc or later; Native AA provides 1.0x | Favors a D3D12 x64 host for a generic cross-vendor backend; still needs valid legacy temporal data. |
| OptiScaler | D3D11/D3D12/Vulkan; expects existing DLSS2+/FSR2+/XeSS-like hooks/contracts | Useful backend/ecosystem reference, not the legacy extraction solution. |

Compatibility claims are scoped by source API, bitness, architecture, GPU/driver where relevant, backend, and flat/VR mode. A single upstream game report or local synthetic probe does not justify `supported`.
