# Depth research

Status: **access mechanisms observed upstream; game-independent selection remains unresolved**.

Depth is not one problem. LTR Bridge must identify the correct scene depth, make it sampleable/copyable, preserve its semantic convention, and determine which render phase it belongs to.

## What ReShade proves

ReShade's current generic depth add-on demonstrates useful API-specific techniques across D3D9, D3D10 and D3D11.

### D3D9

For compatible non-MSAA depth resources, ReShade can replace otherwise unsampleable depth formats with the `INTZ` format and add shader-resource usage. It deliberately avoids some shadow-map-like resources and small depth textures because format substitution can break games.

Its backup path uses `R32F` because `INTZ` itself cannot be used as the render target required by the copy path. This is evidence that “depth found” and “depth transportable” are separate capabilities.

### D3D10 / D3D11

ReShade can make intercepted depth textures typeless and expose compatible shader-resource views. This is much cleaner than the D3D9 format-substitution path, but it still depends on observing resource creation before the game fixes an incompatible resource description.

### Clears, aliases and MSAA

The generic add-on tracks depth bindings, draw workload and clear operations and can make a backup copy before a clear. This matters because the useful scene depth may be cleared or rebound before Present.

For multisampled depth, its generic path requires API/device support for depth-stencil resolve; otherwise that resource cannot simply be treated like a single-sample depth texture.

## Selection problem

A legacy game may have multiple depth-like resources in one frame:

- main world depth;
- shadow maps;
- reflection/refraction passes;
- weapon/first-person depth;
- UI or post-process depth;
- per-eye depth in VR.

ReShade uses heuristics such as resolution/aspect ratio, draw statistics and clear timing to select a useful buffer. Those heuristics are excellent prototyping evidence, but they are not a semantic contract. A reusable LTR Bridge adapter should record *why* a depth resource was selected and allow game/profile-specific overrides.

## Minimum depth metadata

The temporal provider should know or explicitly mark unknown:

- native format and transport format;
- normal vs reversed/inverted Z;
- hardware/non-linear vs already linear depth;
- near/far values when required for reconstruction;
- MSAA sample count and how it was resolved;
- viewport/subrect represented by the depth;
- render pass / clear generation;
- whether the resource includes the same geometry as color and motion vectors;
- per-eye/view identity in VR.

## First controlled tests

The D3D11 harness should deliberately include multiple depth buffers, a shadow pass, an MSAA variant, a clear before Present, reversed Z and a small first-person/overlay pass. The diagnostic view must show the exact depth consumed by the temporal backend and its interpreted convention.
