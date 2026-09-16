param(
    [string]$Configuration = "Release"
)

$ErrorActionPreference = "Stop"
$repoRoot = Split-Path -Parent $PSScriptRoot
$build = Join-Path $repoRoot "build-root/d3d9ex-scene-x86"

& cmake -S $repoRoot -B $build -A Win32 -DLTR_ENABLE_D3D9EX_SCENE_PROBE=ON
if ($LASTEXITCODE -ne 0) { throw "CMake configure failed: $LASTEXITCODE" }
& cmake --build $build --config $Configuration --target ltr_d3d9ex_scene_probe
if ($LASTEXITCODE -ne 0) { throw "CMake build failed: $LASTEXITCODE" }

$probe = Join-Path $build "$Configuration/ltr_d3d9ex_scene_probe.exe"
if (-not (Test-Path $probe)) { throw "D3D9Ex scene probe was not produced" }
& $probe
if ($LASTEXITCODE -ne 0) { throw "D3D9Ex scene probe failed: $LASTEXITCODE" }
