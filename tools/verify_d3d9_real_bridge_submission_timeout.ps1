param(
    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Release',
    [int]$SubmissionTimeoutMs = 100,
    [int]$InitialStallMs = 1500
)

$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
$x86Build = Join-Path $repoRoot 'build-root/d3d9-real-bridge-transport-x86'
$x64Build = Join-Path $repoRoot 'build-root/d3d9-real-bridge-transport-x64'

if ($SubmissionTimeoutMs -lt 1) { throw 'SubmissionTimeoutMs must be positive' }
if ($InitialStallMs -le $SubmissionTimeoutMs) {
    throw 'InitialStallMs must exceed SubmissionTimeoutMs'
}

& cmake --build $x86Build --config $Configuration --target ltr_d3d9_real_bridge_producer
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
& cmake --build $x64Build --config $Configuration --target ltr_d3d9_real_bridge_sink
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

$producer = Join-Path $x86Build "$Configuration/ltr_d3d9_real_bridge_producer.exe"
$sink = Join-Path $x64Build "$Configuration/ltr_d3d9_real_bridge_sink.exe"
$stopwatch = [System.Diagnostics.Stopwatch]::StartNew()
$ErrorActionPreference = 'Continue'
$output = (& $producer `
    --sink $sink `
    --frames 3 `
    --width 64 `
    --height 64 `
    --initial-stall-ms $InitialStallMs `
    --submission-timeout-ms $SubmissionTimeoutMs 2>&1 | Out-String)
$exitCode = $LASTEXITCODE
$ErrorActionPreference = 'Stop'
$stopwatch.Stop()

Write-Output $output.TrimEnd()
Write-Output "event=submission_timeout_probe exit=$exitCode elapsed_ms=$($stopwatch.ElapsedMilliseconds)"

if ($exitCode -ne 8) {
    throw "Expected bounded submission timeout exit 8, got $exitCode"
}
if ($output -notmatch 'stage=submission_timeout frame=2') {
    throw 'Producer did not report the expected pre-submit timeout at the first slot reuse'
}
if ($stopwatch.ElapsedMilliseconds -ge $InitialStallMs) {
    throw 'Submission timeout did not bound the stalled producer before the sink stall elapsed'
}

Write-Output 'RESULT PASS'
