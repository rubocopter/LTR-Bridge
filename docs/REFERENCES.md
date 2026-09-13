# References and research snapshot

Research date: 2026-09-13. External projects change quickly; re-check versions before using a conclusion operationally.

## DLSS5-Feeder

- Repository: https://github.com/jlrouzies-fr/DLSS5-Feeder
- Releases: https://github.com/jlrouzies-fr/DLSS5-Feeder/releases
- Stable version observed: `v0.15.1`, short commit `3f62485`, released 2026-09-09.
- Newest prerelease observed: `v1.16.0-beta.1`, short commit `55c5bca`, released 2026-09-10. It retains IPC protocol v9; 32-bit installs require the matching add-on and `host64` binary from the same build.
- Key current topics: 32-bit helper, D3D10 relay, host-created/shared resources, protocol versioning, DLAA-style 1:1 contract, diagnostics.
- License observed: MIT for repository code; third-party components keep their own licenses.

## DLSS5-Swapper

- Repository: https://github.com/rakanki911/DLSS5-Swapper
- Releases: https://github.com/rakanki911/DLSS5-Swapper/releases
- Role in this research: deployment/API detection, x86/x64 routing, component configuration, backup/restore and compatibility handling.
- License observed: MIT for Swapper itself; third-party notices retain component-specific licenses.

## OptiScaler

- Repository: https://github.com/optiscaler/OptiScaler
- Releases: https://github.com/optiscaler/OptiScaler/releases
- Stable version observed: `v0.9.4`, short commit `7534ad0`.
- Current documented APIs: D3D11, D3D12 and Vulkan with backend-specific paths.
- Research use: modern temporal-backend substitution and D3D11/D3D12 interop lessons, not legacy temporal-data extraction by itself.

## ReShade

- Repository: https://github.com/crosire/reshade
- Current version observed in 2026 ecosystem references: 6.8.0.
- Commit history observed through 2026-09-10; `a33de92` is shown as the latest commit on that date in the GitHub history view.
- Generic depth example: https://github.com/crosire/reshade/blob/main/examples/09-depth/generic_depth_addon.cpp
- API device definitions: https://github.com/crosire/reshade/blob/main/include/reshade_api_device.hpp
- Primary project license: BSD-3-Clause. Public API headers such as `reshade.hpp` and `reshade_api.hpp` declare BSD-3-Clause OR MIT. Verify the exact file/component before reuse or redistribution.
- Generic depth add-on: https://github.com/crosire/reshade/blob/main/examples/09-depth/generic_depth_addon.cpp — current reference for D3D9 `INTZ`, D3D10/11 typeless depth access, backup-before-clear logic and MSAA depth-resolve capability checks.

## LumeniteFX / image-space motion estimation

- Repository: https://github.com/umar-afzaal/LumeniteFX
- Current project description documents a Kernel pre-effect that computes reconstructed normals, motion vectors and motion confidence, plus LumaFlow dense real-time motion estimation.
- DLSS5-Feeder currently recommends Lumenite Kernel and documents consuming its 1/8-resolution flow plus confidence output. Treat this as evidence for a useful fallback/baseline, not renderer-native motion semantics.

## NVIDIA Streamline / DLSS

- Streamline repository: https://github.com/NVIDIA-RTX/Streamline
- DLSS programming guide: https://github.com/NVIDIA-RTX/Streamline/blob/main/docs/ProgrammingGuideDLSS.md
- General programming guide: https://github.com/NVIDIA-RTX/Streamline/blob/main/docs/ProgrammingGuide.md
- DLSS header: https://github.com/NVIDIA-RTX/Streamline/blob/main/include/sl_dlss.h
- NVIDIA RTX SDK license text used by NGX/related SDK material: https://github.com/NVIDIA-RTX/Streamline/blob/main/external/ngx-sdk/license.txt
- Official Streamline/DLSS integration page: https://developer.nvidia.com/rtx/streamline/get-started
- Streamline programming guide currently states Windows 10 RS3 64-bit or newer for SL features; the current build defaults to `amd64` and also supports ARM64/ARM64EC targets.

License note: NVIDIA SDK/runtime distribution is governed by NVIDIA terms, not by the permissive licenses of surrounding community projects. RTX SDK license snapshot reviewed: v. March 14, 2024. It permits distribution of covered SDK software/materials when incorporated in object-code form into an application subject to its requirements, prohibits standalone SDK redistribution, requires material additional application functionality, and contains DLSS/NGX-specific notification/attribution/hardware terms. Re-check the then-current license and the exact binaries selected before any release.

## AMD FidelityFX

- FidelityFX SDK repository: https://github.com/GPUOpen-LibrariesAndSDKs/FidelityFX-SDK
- Temporal super-resolution documentation: https://github.com/GPUOpen-LibrariesAndSDKs/FidelityFX-SDK/blob/main/Kits/FidelityFX/docs/techniques/super-resolution-temporal.md
- Current SDK observed: FSR SDK `2.3.0` ("Redstone").
- FSR API documentation: https://github.com/GPUOpen-LibrariesAndSDKs/FidelityFX-SDK/blob/main/Kits/FidelityFX/docs/getting-started/ffx-api.md
- Current FSR API is a five-function ABI delivered through provided signed DLLs; its backend-specific functionality is documented as currently DirectX 12. Exact x86 binary support remains to be verified from release artifacts rather than assumed.
- Current signed-binary directory: https://github.com/GPUOpen-LibrariesAndSDKs/FidelityFX-SDK/tree/main/Kits/FidelityFX/signedbin
- Primary SDK license: https://github.com/GPUOpen-LibrariesAndSDKs/FidelityFX-SDK/blob/main/docs/license.md
- Licensing note: the current license permits redistribution of covered software in binary form subject to notices/terms and prohibits reverse engineering/decompilation/disassembly. Source-file exceptions are enumerated separately and must be checked file-by-file if reused.

## Intel XeSS

- SDK repository: https://github.com/intel/xess
- Releases: https://github.com/intel/xess/releases
- XeSS-SR developer guide: https://github.com/intel/xess/blob/main/doc/xess_sr_developer_guide_english.md
- Current release observed: XeSS SDK `v3.0.2`, short commit `8fe81bd`, released 2026-07-24.
- The SR developer guide requires Windows 10/11 x64. D3D12 is cross-vendor with the documented feature requirements; the D3D11 SR implementation is currently limited to Intel Arc or later.
- XeSS 3.0.0 introduced external-memory-heap support for sharing GPU memory with other engine components. Current XeSS-SR D3D12 documentation also allows application-provided temporary heaps/descriptors. These are relevant architectural evidence, not proof that XeSS itself solves LTR Bridge's cross-process transport.
- Primary SDK license: https://github.com/intel/xess/blob/main/LICENSE.txt — Intel Simplified Software License (Version October 2022). It permits redistribution of the binary software without modification subject to reproducing notices/terms, prohibits endorsement using Intel/supplier names without permission, and prohibits reverse engineering/decompilation/disassembly or modification. Third-party components have separate notices.

## Microsoft graphics interop documentation

- D3D11/D3D12 shared handles: https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12device-createsharedhandle
- D3D12 shared heaps: https://learn.microsoft.com/en-us/windows/win32/direct3d12/shared-heaps
- D3D11 shared fence: https://learn.microsoft.com/en-us/windows/win32/api/d3d11_4/nf-d3d11_4-id3d11device5-opensharedfence
- Windows graphics surface sharing: https://learn.microsoft.com/en-us/windows/win32/direct3darticles/surface-sharing-between-windows-graphics-apis
- D3D10 resource sharing flags: https://learn.microsoft.com/en-us/windows/win32/api/d3d10/ne-d3d10-d3d10_resource_misc_flag
- D3D9 resource sharing overview: https://learn.microsoft.com/en-us/windows/win32/direct3d9/dx9lh
- D3D11 `OpenSharedResource`, including the documented D3D9 -> D3D11 texture path: https://learn.microsoft.com/en-us/windows/win32/api/d3d11/nf-d3d11-id3d11device-opensharedresource
- Cross-API surface-sharing overview, which scopes unsynchronized sharing to D3D9Ex: https://learn.microsoft.com/en-us/windows/win32/direct3darticles/surface-sharing-between-windows-graphics-apis
- D3D11-on-12: https://learn.microsoft.com/en-us/windows/win32/direct3d12/direct3d-11-on-12

## OpenXR

- OpenXR 1.1 specification/man pages: https://registry.khronos.org/OpenXR/specs/1.1/html/xrspec.html
- `xrWaitFrame`: https://registry.khronos.org/OpenXR/specs/1.1/man/html/xrWaitFrame.html
- Khronos `hello_xr` reference implementation: https://github.com/KhronosGroup/OpenXR-SDK-Source/blob/main/src/tests/hello_xr/openxr_program.cpp
- Relevance: authoritative evidence for per-view recommended extents, `predictedDisplayTime`, and locating per-view poses for the same target display time before frame submission.

## dgVoodoo2

- Project site: http://dege.freeweb.hu/dgVoodoo2/dgVoodoo2/
- Current ecosystem version observed in DLSS5-Swapper: 2.87.4.
- Primary redistribution terms reviewed: individual dgVoodoo files may be shipped as part of a specific game or game mod; standalone redistribution is expected to preserve the complete original package, and the author does not permit bundling dgVoodoo into a general-purpose launcher/framework intended to apply it across arbitrary applications. This materially affects any future LTR Bridge packaging strategy and should be re-checked against the then-current dgVoodoo readme before release.

## D3D8 translation

- `crosire/d3d8to9`: https://github.com/crosire/d3d8to9
- ReShade setup source currently detects D3D8 executables and directs users to `d3d8to9` before using the D3D9 path.
- `d3d8to9` performs D3D8 -> D3D9 API/shader translation and is BSD-2-Clause. Its own documentation warns that rendering behavior may differ from native D3D8 on modern systems.
- `d3d8to9ex`: https://github.com/RibShark/d3d8to9ex — a small third-party D3D9Ex-targeting fork; research lead only, not a selected dependency.

## Research discipline

When a design depends on an upstream implementation detail, replace moving `main` links with an exact commit or release tag in the relevant document before implementation begins.

## Real-game temporal integration case studies

### BioShock VR DLSS/DLAA

- Repository: https://github.com/Beren5556/BioShock-VR-DLSS-DLAA
- Reviewed release: `v0.2.17-en`.
- Pinned source: `8671fc87c4646140419ea64bd6e60d59fcac4723`.
- Research use: x86 D3D11 -> independent x64 per-eye NGX helpers, strict temporal identity/rejection, camera+depth motion reconstruction and stereo contamination testing.
- Full notes: `docs/CASE_STUDY_BIOSHOCK_VR.md`.

### Rogue Trader EnhancedGraphics

- Original repository: https://github.com/cstamford/RogueTrader_DLSS — baseline reviewed at `f2444b09ecee649e39851715cb5133e7020e159c`.
- Current public fork reviewed: https://github.com/BradyBrenot/RogueTrader_DLSS
- Reviewed release: `v2.2`, source commit `01b1cd816db08f2b1c6c68b1319c6f44dd61bdd6`, published 2026-06-13.
- Research use: renderer-native D3D11 color/depth/MV access, real camera jitter, true internal-resolution control, pre-postprocess reconstruction and resolution-dependent engine fixes.
- Current known MV limitation: open issue `#2` documents cloth/cape ghosting because cloth is not represented correctly in the motion-vector path.
- Licensing note: no root repository license was detected in the reviewed public snapshots. Treat source as reference-only unless explicit reuse terms are obtained; NVIDIA DLSS/NGX remains separately licensed.
- Full notes: `docs/CASE_STUDY_ROGUE_TRADER_DLSS.md`.
