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
$stdout = Join-Path ([IO.Path]::GetTempPath()) "ltr_parent_exit_stdout_$PID.log"
$stderr = Join-Path ([IO.Path]::GetTempPath()) "ltr_parent_exit_stderr_$PID.log"
$producerProcess = $null
$sinkPid = $null
$transportLog = $null

try {
    $arguments = @(
        '--sink', $sink,
        '--frames', '3',
        '--width', '64',
        '--height', '64',
        '--initial-stall-ms', '750',
        '--submission-timeout-ms', '10000'
    )
    $producerProcess = Start-Process -FilePath $producer -ArgumentList $arguments -PassThru `
        -RedirectStandardOutput $stdout -RedirectStandardError $stderr
    $transportLog = Join-Path ([IO.Path]::GetTempPath()) "ltr_d3d9_real_bridge_transport_$($producerProcess.Id).log"

    $deadline = [DateTime]::UtcNow.AddSeconds(5)
    while ([DateTime]::UtcNow -lt $deadline) {
        if (Test-Path $transportLog) {
            $text = Get-Content -Raw $transportLog
            if ($text -match 'stage=initialized') { break }
        }
        Start-Sleep -Milliseconds 20
    }
    if (-not (Test-Path $transportLog) -or (Get-Content -Raw $transportLog) -notmatch 'stage=initialized') {
        throw 'Sink did not reach initialized state before the parent-exit probe deadline'
    }

    $sinkProcess = Get-CimInstance Win32_Process -Filter "ParentProcessId = $($producerProcess.Id)" |
        Where-Object { $_.ExecutablePath -eq $sink } |
        Select-Object -First 1
    if (-not $sinkProcess) { throw 'Could not identify the sink child process' }
    $sinkPid = [int]$sinkProcess.ProcessId

    Stop-Process -Id $producerProcess.Id -Force
    $producerProcess = $null

    $deadline = [DateTime]::UtcNow.AddSeconds(5)
    while ([DateTime]::UtcNow -lt $deadline) {
        if (-not (Get-Process -Id $sinkPid -ErrorAction SilentlyContinue)) { break }
        Start-Sleep -Milliseconds 20
    }
    if (Get-Process -Id $sinkPid -ErrorAction SilentlyContinue) {
        throw 'Sink remained alive after its parent exited'
    }

    $text = Get-Content -Raw $transportLog
    Write-Output $text.TrimEnd()
    if ($text -notmatch 'stage=producer_ready frame=\d+ .*reason=parent_exited RESULT FAIL') {
        throw 'Sink did not classify parent exit while waiting for the next ready value'
    }
    if ($text -match 'RESULT PASS') { throw 'Parent-exit probe produced a PASS marker' }
    Write-Output 'RESULT PASS'
}
finally {
    if ($producerProcess -and -not $producerProcess.HasExited) {
        Stop-Process -Id $producerProcess.Id -Force -ErrorAction SilentlyContinue
    }
    if ($sinkPid -and (Get-Process -Id $sinkPid -ErrorAction SilentlyContinue)) {
        Stop-Process -Id $sinkPid -Force -ErrorAction SilentlyContinue
    }
    Remove-Item $stdout, $stderr -Force -ErrorAction SilentlyContinue
    if ($transportLog) { Remove-Item $transportLog -Force -ErrorAction SilentlyContinue }
}
