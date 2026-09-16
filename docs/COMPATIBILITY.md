# Evidence-based compatibility matrix

Snapshot: 2026-09-16. This is a research matrix, not a product support table.

States: `unknown`, `observed-upstream`, `planned`, `implemented`, `host-tested`, `live-tested`, `visual-validated`, `performance-validated`, `vr-headset-validated`, `supported`.

| Source API / architecture | Direct interception | Translation/relay | x86 -> x64 helper | Native-res temporal AA | Real temporal SR | VR | Evidence |
| --- | --- | --- | --- | --- | --- | --- | --- |
| D3D11 x64, in process | planned | not required for first probe | not required | planned | unknown | unknown | Primary SDKs support modern D3D11 paths; local harness not built. |
| D3D11 x86 -> D3D12 x64 host | planned | no translation expected | observed-upstream | unknown | unknown | unknown | DLSS5-Feeder demonstrates GPU-resident cross-bitness transport. |
| D3D10 x86 -> D3D11 relay -> x64 host | planned | observed-upstream relay | observed-upstream | unknown | unknown | unknown | Current DLSS5-Feeder D3D10 route. |
| D3D10 direct to modern host | unknown | n/a | unknown | unknown | unknown | unknown | D3D10 lacks the D3D11 NT-handle/fence path used by current bridge designs. |
| D3D9 classic -> D3D11 relay | host-tested negative boundary | current-host shared creation rejected | unknown | unknown | unknown | unknown | Win32 probe: all tested `CreateTexture(..., pSharedHandle)` cases return `D3DERR_INVALIDCALL` on the current RTX 4070 Ti host; five repetitions stable. Do not generalize beyond this host/runtime. |
| D3D9Ex -> D3D11 relay -> x86/x64 D3D12 host | host-tested | current-host R10 relay works end to end | host-tested | unknown | unknown | unknown | Bound local D3D9Ex R10 render target -> clear/restore -> `StretchRect` -> shared R10 relay -> private D3D11 fullscreen conversion -> existing shared RGBA8 transport -> x64 D3D12. Five repetitions pass at both `64x64 -> 96x72` and `1920x1080 -> 2560x1440`, with one `ResetEx`/resource transition and zero mismatches per run. RGBA16F is also proven in the independent relay probe; documented RGBA8 fails here, while BGRA8 remains a driver-specific control. |
| D3D9 -> dgVoodoo2 -> D3D11 | planned comparison | observed-upstream | observed-upstream downstream | observed-upstream in Feeder-style stack | not demonstrated as real SR | unknown | Existing community tooling uses this route. |
| D3D8 native | unknown | planned | unknown | unknown | unknown | unknown | Requires D3D8-specific investigation. |
| D3D8 -> dgVoodoo2 -> D3D11 | planned | observed in ecosystem | possible downstream | unknown | unknown | unknown | Deployment tools use this route; LTR Bridge has not validated it. |
| Stereo/OpenXR controlled harness | n/a | n/a | optional | planned | unknown | planned | No local prototype yet. |

## Backend/API observations

| Backend family | Current useful API evidence | Legacy relevance |
| --- | --- | --- |
| NVIDIA DLSS/DLAA via Streamline/NGX | Streamline currently targets 64-bit Windows and modern D3D11/D3D12/Vulkan-era integration with explicit temporal inputs | Strong reason for an x64 modern host when the source game is x86; still requires correct legacy data extraction. |
| FidelityFX temporal upscaling | Current FSR SDK is 2.3.0; the current FSR API is delivered through signed DX12 DLLs. The exact v2.3.0 loader/upscaler and the other signed DX12 DLLs were verified as PE x64 (`0x8664`). | Useful x64 D3D12-host backend candidate after a legacy provider exists. The v2.3.0 signed runtime cannot be loaded directly into an x86 process; future releases must be rechecked rather than assumed identical. |
| XeSS-SR / Native AA | XeSS SDK 3.0.2 requires Windows x64; D3D12 is cross-vendor, while its documented D3D11 SR path is limited to Intel Arc or later; Native AA provides 1.0x | Favors a D3D12 x64 host for a generic cross-vendor backend; still needs valid legacy temporal data. |
| OptiScaler | D3D11/D3D12/Vulkan; expects existing DLSS2+/FSR2+/XeSS-like hooks/contracts | Useful backend/ecosystem reference, not the legacy extraction solution. |

Compatibility claims are scoped by source API, bitness, architecture, GPU/driver where relevant, backend, and flat/VR mode. A single upstream game report or local synthetic probe does not justify `supported`.
