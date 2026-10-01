param(
    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Release',
    [int]$PauseMs = 11000
)

$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
$x86Build = Join-Path $repoRoot 'build-root/d3d9-real-bridge-transport-x86'
$x64Build = Join-Path $repoRoot 'build-root/d3d9-real-bridge-transport-x64'

if ($PauseMs -le 10000) { throw 'PauseMs must exceed ten seconds' }

& cmake --build $x86Build --config $Configuration --target ltr_d3d9_real_bridge_producer
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
& cmake --build $x64Build --config $Configuration --target ltr_d3d9_real_bridge_sink
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

$producer = Join-Path $x86Build "$Configuration/ltr_d3d9_real_bridge_producer.exe"
$sink = Join-Path $x64Build "$Configuration/ltr_d3d9_real_bridge_sink.exe"
$stopwatch = [System.Diagnostics.Stopwatch]::StartNew()
$savedPreference = $ErrorActionPreference
$ErrorActionPreference = 'Continue'
$output = (& $producer `
    --sink $sink `
    --frames 4 `
    --width 64 `
    --height 64 `
    --initial-stall-ms 0 `
    --interframe-pause-after 2 `
    --interframe-pause-ms $PauseMs 2>&1 | Out-String)
$exitCode = $LASTEXITCODE
$ErrorActionPreference = $savedPreference
$stopwatch.Stop()

Write-Output $output.TrimEnd()
Write-Output "event=interframe_gap_probe exit=$exitCode elapsed_ms=$($stopwatch.ElapsedMilliseconds) pause_ms=$PauseMs"

if ($exitCode -ne 0) { throw "Interframe-gap producer failed with exit $exitCode" }
if ($stopwatch.ElapsedMilliseconds -lt $PauseMs) { throw 'Requested interframe pause was not exercised' }
if ($output -notmatch 'event=producer_complete frames=4 final_done=4 .*RESULT PASS') {
    throw 'Transport did not resume and complete after the long interframe gap'
}

Write-Output 'RESULT PASS'
