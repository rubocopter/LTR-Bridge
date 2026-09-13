# Evidence-based compatibility matrix

Snapshot: 2026-09-13. This is a research matrix, not a product support table.

States: `unknown`, `observed-upstream`, `planned`, `implemented`, `host-tested`, `live-tested`, `visual-validated`, `performance-validated`, `vr-headset-validated`, `supported`.

| Source API / architecture | Direct interception | Translation/relay | x86 -> x64 helper | Native-res temporal AA | Real temporal SR | VR | Evidence |
| --- | --- | --- | --- | --- | --- | --- | --- |
| D3D11 x64, in process | planned | not required for first probe | not required | planned | unknown | unknown | Primary SDKs support modern D3D11 paths; local harness not built. |
| D3D11 x86 -> D3D12 x64 host | planned | no translation expected | observed-upstream | unknown | unknown | unknown | DLSS5-Feeder demonstrates GPU-resident cross-bitness transport. |
| D3D10 x86 -> D3D11 relay -> x64 host | planned | observed-upstream relay | observed-upstream | unknown | unknown | unknown | Current DLSS5-Feeder D3D10 route. |
| D3D10 direct to modern host | unknown | n/a | unknown | unknown | unknown | unknown | D3D10 lacks the D3D11 NT-handle/fence path used by current bridge designs. |
| D3D9 direct interception + custom relay | planned | planned relay research | unknown | unknown | unknown | unknown | D3D9 sharing exists but cannot simply be opened as D3D12 resources. |
| D3D9 -> dgVoodoo2 -> D3D11 | planned comparison | observed-upstream | observed-upstream downstream | observed-upstream in Feeder-style stack | not demonstrated as real SR | unknown | Existing community tooling uses this route. |
| D3D8 native | unknown | planned | unknown | unknown | unknown | unknown | Requires D3D8-specific investigation. |
| D3D8 -> dgVoodoo2 -> D3D11 | planned | observed in ecosystem | possible downstream | unknown | unknown | unknown | Deployment tools use this route; LTR Bridge has not validated it. |
| Stereo/OpenXR controlled harness | n/a | n/a | optional | planned | unknown | planned | No local prototype yet. |

## Backend/API observations

| Backend family | Current useful API evidence | Legacy relevance |
| --- | --- | --- |
| NVIDIA DLSS/DLAA via modern SDK path | Modern D3D11/D3D12/Vulkan-era integration with explicit temporal inputs | Requires adapter/provider work to synthesize missing legacy inputs; x86 may require helper depending on component availability. |
| FidelityFX temporal upscaling | Modern temporal input contract with depth/MV/jitter and optional reactive information | Potential backend after a legacy provider exists; does not solve extraction itself. |
| XeSS-SR / Native AA | D3D11, D3D12, Vulkan documented; Native AA provides 1.0x mode | Attractive second controlled 1:1 backend; still needs valid legacy temporal data. |
| OptiScaler | D3D11/D3D12/Vulkan; expects existing DLSS2+/FSR2+/XeSS-like hooks/contracts | Useful backend/ecosystem reference, not the legacy extraction solution. |

Compatibility claims are scoped by source API, bitness, architecture, GPU/driver where relevant, backend, and flat/VR mode. A single upstream game report or local synthetic probe does not justify `supported`.
