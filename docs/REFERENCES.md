# References and research snapshot

Research date: 2026-09-13. External projects change quickly; re-check versions before using a conclusion operationally.

## DLSS5-Feeder

- Repository: https://github.com/jlrouzies-fr/DLSS5-Feeder
- Releases: https://github.com/jlrouzies-fr/DLSS5-Feeder/releases
- Stable version observed: `v0.15.1`, short commit `3f62485`, released 2026-09-09.
- Current release page also lists a newer beta line; do not silently treat beta behavior as stable behavior.
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
- License: BSD-3-Clause OR MIT for API headers/source portions as declared in files; verify exact file/component before redistribution.

## NVIDIA Streamline / DLSS

- Streamline repository: https://github.com/NVIDIA-RTX/Streamline
- DLSS programming guide: https://github.com/NVIDIA-RTX/Streamline/blob/main/docs/ProgrammingGuideDLSS.md
- General programming guide: https://github.com/NVIDIA-RTX/Streamline/blob/main/docs/ProgrammingGuide.md
- DLSS header: https://github.com/NVIDIA-RTX/Streamline/blob/main/include/sl_dlss.h
- NVIDIA RTX SDK license text used by NGX/related SDK material: https://github.com/NVIDIA-RTX/Streamline/blob/main/external/ngx-sdk/license.txt
- Official Streamline/DLSS integration page: https://developer.nvidia.com/rtx/streamline/get-started

License note: NVIDIA SDK/runtime distribution is governed by NVIDIA terms, not by the permissive licenses of surrounding community projects. Review the current SDK supplement before shipping binaries.

## AMD FidelityFX

- FidelityFX SDK repository: https://github.com/GPUOpen-LibrariesAndSDKs/FidelityFX-SDK
- Temporal super-resolution documentation: https://github.com/GPUOpen-LibrariesAndSDKs/FidelityFX-SDK/blob/main/Kits/FidelityFX/docs/techniques/super-resolution-temporal.md

## Intel XeSS

- SDK repository: https://github.com/intel/xess
- XeSS-SR developer guide: https://github.com/intel/xess/blob/main/doc/xess_sr_developer_guide_english.md
- Current repository identifies the package as XeSS 3; the SR developer guide documents modern SR and Native Anti-Aliasing behavior.

## Microsoft graphics interop documentation

- D3D11/D3D12 shared handles: https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12device-createsharedhandle
- D3D12 shared heaps: https://learn.microsoft.com/en-us/windows/win32/direct3d12/shared-heaps
- D3D11 shared fence: https://learn.microsoft.com/en-us/windows/win32/api/d3d11_4/nf-d3d11_4-id3d11device5-opensharedfence
- Windows graphics surface sharing: https://learn.microsoft.com/en-us/windows/win32/direct3darticles/surface-sharing-between-windows-graphics-apis
- D3D10 resource sharing flags: https://learn.microsoft.com/en-us/windows/win32/api/d3d10/ne-d3d10-d3d10_resource_misc_flag
- D3D9 resource sharing overview: https://learn.microsoft.com/en-us/windows/win32/direct3d9/dx9lh
- D3D11-on-12: https://learn.microsoft.com/en-us/windows/win32/direct3d12/direct3d-11-on-12

## dgVoodoo2

- Project site: http://dege.freeweb.hu/dgVoodoo2/dgVoodoo2/
- Current ecosystem version observed in DLSS5-Swapper: 2.87.4.
- Primary redistribution terms reviewed: individual dgVoodoo files may be shipped as part of a specific game or game mod; standalone redistribution is expected to preserve the complete original package, and the author does not permit bundling dgVoodoo into a general-purpose launcher/framework intended to apply it across arbitrary applications. This materially affects any future LTR Bridge packaging strategy and should be re-checked against the then-current dgVoodoo readme before release.

## Research discipline

When a design depends on an upstream implementation detail, replace moving `main` links with an exact commit or release tag in the relevant document before implementation begins.
