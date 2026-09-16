param(
    [string]$Configuration = "Release"
)

$ErrorActionPreference = "Stop"
$repoRoot = Split-Path -Parent $PSScriptRoot
$x86Build = Join-Path $repoRoot "build-root/x86-x64-bridge-x86"
$x64Build = Join-Path $repoRoot "build-root/x86-x64-bridge-x64"

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

Invoke-Checked cmake @(
    "-S", $repoRoot,
    "-B", $x86Build,
    "-A", "Win32",
    "-DLTR_ENABLE_X86_X64_BRIDGE_PROBE=ON"
)
Invoke-Checked cmake @(
    "--build", $x86Build,
    "--config", $Configuration,
    "--target", "ltr_x86_x64_bridge_producer"
)

Invoke-Checked cmake @(
    "-S", $repoRoot,
    "-B", $x64Build,
    "-A", "x64",
    "-DLTR_ENABLE_X86_X64_BRIDGE_PROBE=ON"
)
Invoke-Checked cmake @(
    "--build", $x64Build,
    "--config", $Configuration,
    "--target", "ltr_x86_x64_bridge_consumer"
)

$producer = Join-Path $x86Build "$Configuration/ltr_x86_x64_bridge_producer.exe"
$consumer = Join-Path $x64Build "$Configuration/ltr_x86_x64_bridge_consumer.exe"
if (-not (Test-Path $producer) -or -not (Test-Path $consumer)) {
    throw "bridge probe executables were not produced"
}

Write-Host "=== positive multiframe/generation-size probe ==="
Invoke-Checked $consumer @("--producer", $producer)

foreach ($negative in @("protocol", "adapter", "resource-contract", "host-stall", "dynamic-control")) {
    Write-Host "=== negative probe: $negative ==="
    Invoke-Checked $consumer @("--producer", $producer, "--negative", $negative)
}
