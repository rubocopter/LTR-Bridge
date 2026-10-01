param(
    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Release',
    [int]$Repetitions = 8
)

$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
$x64Build = Join-Path $repoRoot 'build-root/d3d9-real-bridge-transport-x64'
$logPath = Join-Path $env:TEMP "ltr_bridge_bootstrap_failure_$PID.log"

if ($Repetitions -lt 2) {
    throw 'Repetitions must be at least 2'
}

& cmake -S $repoRoot -B $x64Build -A x64 -DLTR_ENABLE_D3D9_REAL_BRIDGE_SINK=ON
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
& cmake --build $x64Build --config $Configuration --target ltr_d3d9_real_bridge_sink
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

$sink = Join-Path $x64Build "$Configuration/ltr_d3d9_real_bridge_sink.exe"
if (-not (Test-Path $sink)) {
    throw "Bridge sink was not produced: $sink"
}

function Invoke-PublicationFailure {
    Remove-Item -LiteralPath $logPath -Force -ErrorAction SilentlyContinue
    & $sink `
        --bootstrap-write 1 `
        --parent-pid $PID `
        --ready-fence-name "Local\LTRBootstrapFailure_$PID" `
        --protocol-version 1 `
        --frames 2 `
        --generation 1 `
        --width 64 `
        --height 64 `
        --initial-stall-ms 0 `
        --validate-synthetic-pattern 0 `
        --log $logPath
    $exitCode = $LASTEXITCODE
    $log = if (Test-Path $logPath) { Get-Content -LiteralPath $logPath -Raw } else { '' }
    if ($exitCode -eq 0 -or $log -notmatch 'stage=bootstrap_write RESULT FAIL') {
        throw "Expected bootstrap publication failure; exit=$exitCode log=$log"
    }
}

# Warm up process creation/D3D runtime state before establishing the parent
# process handle-count baseline. The sink deliberately receives an invalid
# bootstrap pipe handle, after it has duplicated its three shared handles into
# this process.
Invoke-PublicationFailure
Start-Sleep -Milliseconds 50
$before = (Get-Process -Id $PID).HandleCount

for ($run = 1; $run -le $Repetitions; $run++) {
    Invoke-PublicationFailure
}

Start-Sleep -Milliseconds 50
$after = (Get-Process -Id $PID).HandleCount
$growth = $after - $before
Remove-Item -LiteralPath $logPath -Force -ErrorAction SilentlyContinue

Write-Host "event=bootstrap_failure_handle_count before=$before after=$after growth=$growth repetitions=$Repetitions"
if ($growth -ne 0) {
    throw "Bootstrap publication failure leaked $growth parent handles"
}

Write-Host 'RESULT PASS'
