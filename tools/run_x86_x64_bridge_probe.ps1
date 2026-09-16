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

Write-Host "=== renderer-local to shared-resource GPU-copy probe ==="
Invoke-Checked $consumer @("--producer", $producer, "--renderer-copy")

foreach ($negative in @(
    "protocol",
    "adapter",
    "resource-contract",
    "format-rgba16f",
    "format-rgba32f",
    "format-rgba8uint",
    "host-stall",
    "dynamic-control",
    "client-termination",
    "device-removal"
)) {
    Write-Host "=== negative probe: $negative ==="
    Invoke-Checked $consumer @("--producer", $producer, "--negative", $negative)
}

Write-Host "=== negative probe: host-termination ==="
$hostTerminationOutput = & $consumer --producer $producer --negative host-termination 2>&1
$hostTerminationExit = $LASTEXITCODE
$hostTerminationOutput | ForEach-Object { Write-Host $_ }
$hostTerminationText = $hostTerminationOutput -join "`n"
if ($hostTerminationExit -ne 24) {
    throw "host-termination host exit was $hostTerminationExit instead of 24"
}
if ($hostTerminationText -notmatch "reject=host_terminated detected_by=process_handle" -or
    $hostTerminationText -notmatch "RESULT PASS") {
    throw "host-termination producer did not report clean host-loss detection"
}

Write-Host "=== negative probe: backpressure host termination during slot reuse ==="
$backpressureHostTerminationOutput = & $consumer --producer $producer --negative backpressure-host-termination 2>&1
$backpressureHostTerminationExit = $LASTEXITCODE
$backpressureHostTerminationOutput | ForEach-Object { Write-Host $_ }
$backpressureHostTerminationText = $backpressureHostTerminationOutput -join "`n"
if ($backpressureHostTerminationExit -ne 26) {
    throw "backpressure-host-termination host exit was $backpressureHostTerminationExit instead of 26"
}
if ($backpressureHostTerminationText -notmatch "reject=backpressure_host_terminated detected_by=process_handle" -or
    $backpressureHostTerminationText -notmatch "RESULT PASS") {
    throw "backpressure-host-termination producer did not report clean in-flight host-loss detection"
}

foreach ($depth in @(1, 2)) {
    Write-Host "=== backpressure probe: ring depth $depth ==="
    Invoke-Checked $consumer @(
        "--producer", $producer,
        "--backpressure-depth", [string]$depth
    )
}

Write-Host "=== stereo transport probe ==="
Invoke-Checked $consumer @("--producer", $producer, "--stereo")

Write-Host "=== negative probe: stereo contamination marker ==="
Invoke-Checked $consumer @("--producer", $producer, "--stereo-contamination")

Write-Host "=== stereo temporal-history probe ==="
Invoke-Checked $consumer @("--producer", $producer, "--stereo-history")

Write-Host "=== negative probe: stereo history swap ==="
Invoke-Checked $consumer @("--producer", $producer, "--stereo-history-swap")
