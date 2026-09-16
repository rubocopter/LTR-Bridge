param(
    [string]$Configuration = "Release"
)

$ErrorActionPreference = "Stop"
$repoRoot = Split-Path -Parent $PSScriptRoot
$build = Join-Path $repoRoot "build-root/d3d10-d3d11-relay-x86"

& cmake -S $repoRoot -B $build -A Win32 -DLTR_ENABLE_D3D10_D3D11_RELAY_PROBE=ON
if ($LASTEXITCODE -ne 0) { throw "CMake configure failed: $LASTEXITCODE" }
& cmake --build $build --config $Configuration --target ltr_d3d10_d3d11_relay_probe
if ($LASTEXITCODE -ne 0) { throw "CMake build failed: $LASTEXITCODE" }

$probe = Join-Path $build "$Configuration/ltr_d3d10_d3d11_relay_probe.exe"
if (-not (Test-Path $probe)) { throw "D3D10/D3D11 relay probe was not produced" }
& $probe
if ($LASTEXITCODE -ne 0) { throw "D3D10/D3D11 relay probe failed: $LASTEXITCODE" }
