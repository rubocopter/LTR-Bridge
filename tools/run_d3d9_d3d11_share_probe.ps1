param(
    [string]$Configuration = "Release"
)

$ErrorActionPreference = "Stop"
$repoRoot = Split-Path -Parent $PSScriptRoot
$build = Join-Path $repoRoot "build-root/d3d9-d3d11-share-x86"

& cmake -S $repoRoot -B $build -A Win32 -DLTR_ENABLE_D3D9_D3D11_SHARE_PROBE=ON
if ($LASTEXITCODE -ne 0) { throw "CMake configure failed: $LASTEXITCODE" }
& cmake --build $build --config $Configuration --target ltr_d3d9_d3d11_share_probe
if ($LASTEXITCODE -ne 0) { throw "CMake build failed: $LASTEXITCODE" }

$probe = Join-Path $build "$Configuration/ltr_d3d9_d3d11_share_probe.exe"
if (-not (Test-Path $probe)) { throw "D3D9/D3D11 sharing probe was not produced" }
& $probe
if ($LASTEXITCODE -ne 0) { throw "D3D9/D3D11 sharing probe failed: $LASTEXITCODE" }
