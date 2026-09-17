param(
    [string]$Configuration = "Release",
    [int]$Repetitions = 5,
    [int]$InitialStallMs = 50,
    [int]$Width = 64,
    [int]$Height = 64,
    [int]$Frames = 12
)

$ErrorActionPreference = "Stop"
$repoRoot = Split-Path -Parent $PSScriptRoot
$x86Build = Join-Path $repoRoot "build-root/d3d9-real-bridge-transport-x86"
$x64Build = Join-Path $repoRoot "build-root/d3d9-real-bridge-transport-x64"

function Invoke-Checked {
    param(
        [Parameter(Mandatory = $true)][string]$Program,
        [Parameter(Mandatory = $true)][string[]]$Arguments
    )

    & $Program @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "$Program failed with exit code $LASTEXITCODE"
    }
}

if ($Repetitions -lt 1) {
    throw "Repetitions must be at least 1"
}
if ($InitialStallMs -lt 0) {
    throw "InitialStallMs cannot be negative"
}
if ($Width -lt 1 -or $Height -lt 1) {
    throw "Width and Height must be at least 1"
}
if ($Frames -lt 2) {
    throw "Frames must be at least 2 for the two-slot probe"
}

Invoke-Checked cmake @(
    "-S", $repoRoot,
    "-B", $x86Build,
    "-A", "Win32",
    "-DLTR_ENABLE_D3D9_REAL_BRIDGE_PROBE=ON"
)
Invoke-Checked cmake @(
    "--build", $x86Build,
    "--config", $Configuration,
    "--target", "ltr_d3d9_real_bridge_producer"
)

Invoke-Checked cmake @(
    "-S", $repoRoot,
    "-B", $x64Build,
    "-A", "x64",
    "-DLTR_ENABLE_D3D9_REAL_BRIDGE_SINK=ON"
)
Invoke-Checked cmake @(
    "--build", $x64Build,
    "--config", $Configuration,
    "--target", "ltr_d3d9_real_bridge_sink"
)

$producer = Join-Path $x86Build "$Configuration/ltr_d3d9_real_bridge_producer.exe"
$sink = Join-Path $x64Build "$Configuration/ltr_d3d9_real_bridge_sink.exe"
if (-not (Test-Path $producer) -or -not (Test-Path $sink)) {
    throw "real bridge transport probe executables were not produced"
}

for ($run = 1; $run -le $Repetitions; $run++) {
    Write-Host "=== real-bridge transport probe $run/$Repetitions ==="
    Invoke-Checked $producer @(
        "--sink", $sink,
        "--frames", [string]$Frames,
        "--width", [string]$Width,
        "--height", [string]$Height,
        "--initial-stall-ms", [string]$InitialStallMs
    )
}
