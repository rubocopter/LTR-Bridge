param(
    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Release'
)

$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
$x86Build = Join-Path $repoRoot 'build-root/d3d9-real-bridge-transport-x86'
$x64Build = Join-Path $repoRoot 'build-root/d3d9-real-bridge-transport-x64'

& cmake --build $x86Build --config $Configuration --target ltr_d3d9_real_bridge_producer
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
& cmake --build $x64Build --config $Configuration --target ltr_d3d9_real_bridge_sink
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

$producer = Join-Path $x86Build "$Configuration/ltr_d3d9_real_bridge_producer.exe"
$sink = Join-Path $x64Build "$Configuration/ltr_d3d9_real_bridge_sink.exe"

function Invoke-ExpectedFailure {
    param(
        [Parameter(Mandatory = $true)][string]$Fault,
        [Parameter(Mandatory = $true)][string]$RequiredPattern
    )

    $savedPreference = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    $output = (& $producer `
        --sink $sink `
        --frames 3 `
        --width 64 `
        --height 64 `
        --initial-stall-ms 0 `
        --fault-injection $Fault 2>&1 | Out-String)
    $exitCode = $LASTEXITCODE
    $ErrorActionPreference = $savedPreference

    Write-Host "event=fault_injection_probe kind=$Fault exit=$exitCode"
    Write-Host $output.TrimEnd()
    if ($exitCode -eq 0) {
        throw "Fault injection '$Fault' was incorrectly accepted"
    }
    if ($output -notmatch $RequiredPattern) {
        throw "Fault injection '$Fault' did not reach expected failure path '$RequiredPattern'"
    }
    if ($output -match 'RESULT PASS') {
        throw "Fault injection '$Fault' produced a PASS marker"
    }
}

Invoke-ExpectedFailure -Fault 'resource0-open' -RequiredPattern 'stage=open_transport slot=0 .*RESULT FAIL'
Invoke-ExpectedFailure -Fault 'done-fence-open' -RequiredPattern 'stage=open_done_fence RESULT FAIL'
Invoke-ExpectedFailure -Fault 'consumer-copy' -RequiredPattern 'stage=signal_done frame=0 .*RESULT FAIL'
Invoke-ExpectedFailure -Fault 'final-completion' -RequiredPattern 'stage=completion .*RESULT FAIL'
Invoke-ExpectedFailure -Fault 'device-removal' -RequiredPattern 'stage=device_removed .*RESULT FAIL'

Write-Host 'RESULT PASS'
