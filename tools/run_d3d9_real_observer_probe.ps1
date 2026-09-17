param(
    [Parameter(Mandatory = $true)]
    [string]$GameExe,
    [int]$TimeoutSeconds = 20,
    [string]$Configuration = "Release",
    [switch]$PromoteToD3D9Ex,
    [switch]$TestSharedRelay,
    [switch]$TestX86X64Bridge,
    [switch]$TestMultiframeBridge,
    [switch]$RequireResetGeneration,
    [int]$ResetTimeoutSeconds = 90,
    [int]$ResetBridgeStallMs = 15000,
    [switch]$CleanupOnly
)

$ErrorActionPreference = "Stop"
$repoRoot = Split-Path -Parent $PSScriptRoot
$gameExePath = (Resolve-Path $GameExe).Path
$gameDir = Split-Path -Parent $gameExePath
$build = Join-Path $repoRoot "build-root/d3d9-real-observer-x86"
$bridgeBuild = Join-Path $repoRoot "build-root/d3d9-real-bridge-x64"
$stageProxy = Join-Path $gameDir "d3dx9_29.dll"
$stageReal = Join-Path $gameDir "ltr_d3dx9_29_real.dll"
$stageBootstrap = Join-Path $gameDir "ltr_d3d9_real_observer_bootstrap.dll"

if ($TestMultiframeBridge -and -not $PromoteToD3D9Ex) {
    throw "TestMultiframeBridge requires PromoteToD3D9Ex"
}
if ($TestMultiframeBridge -and ($TestSharedRelay -or $TestX86X64Bridge)) {
    throw "TestMultiframeBridge must run alone so its lifecycle evidence is unambiguous"
}
if ($RequireResetGeneration -and -not $TestMultiframeBridge) {
    throw "RequireResetGeneration requires TestMultiframeBridge"
}
if ($ResetBridgeStallMs -lt 0 -or $ResetBridgeStallMs -gt 60000) {
    throw "ResetBridgeStallMs must be between 0 and 60000"
}

function Get-PeMachine([string]$Path) {
    $stream = [System.IO.File]::Open($Path, [System.IO.FileMode]::Open, [System.IO.FileAccess]::Read, [System.IO.FileShare]::ReadWrite)
    try {
        $reader = New-Object System.IO.BinaryReader($stream)
        if ($reader.ReadUInt16() -ne 0x5A4D) { return 0 }
        $stream.Position = 0x3C
        $peOffset = $reader.ReadUInt32()
        $stream.Position = $peOffset
        if ($reader.ReadUInt32() -ne 0x00004550) { return 0 }
        return $reader.ReadUInt16()
    }
    finally {
        $stream.Dispose()
    }
}

function Remove-ProbeFile([string]$Path, [string]$ExpectedHash) {
    if (-not (Test-Path $Path)) { return }
    if ($ExpectedHash -and (Get-FileHash $Path -Algorithm SHA256).Hash -ne $ExpectedHash) {
        throw "Refusing to remove changed staged file: $Path"
    }
    for ($attempt = 0; $attempt -lt 20; ++$attempt) {
        try {
            Remove-Item $Path -Force -ErrorAction Stop
            return
        }
        catch {
            if ($attempt -eq 19) { throw }
            Start-Sleep -Milliseconds 250
        }
    }
}

if ((Get-PeMachine $gameExePath) -ne 0x14C) {
    throw "Real D3D9 observer currently requires an x86 game executable"
}

& cmake -S $repoRoot -B $build -A Win32 -DLTR_ENABLE_D3D9_REAL_OBSERVER_PROBE=ON
if ($LASTEXITCODE -ne 0) { throw "CMake configure failed" }
& cmake --build $build --config $Configuration --target ltr_d3d9_real_observer_probe ltr_d3d9_real_observer_bootstrap
if ($LASTEXITCODE -ne 0) { throw "Observer build failed" }

$bridgeSink = $null
if ($TestX86X64Bridge -or $TestMultiframeBridge) {
    & cmake -S $repoRoot -B $bridgeBuild -A x64 -DLTR_ENABLE_D3D9_REAL_BRIDGE_SINK=ON
    if ($LASTEXITCODE -ne 0) { throw "Real bridge sink CMake configure failed" }
    & cmake --build $bridgeBuild --config $Configuration --target ltr_d3d9_real_bridge_sink
    if ($LASTEXITCODE -ne 0) { throw "Real bridge sink build failed" }
    $bridgeSink = Join-Path $bridgeBuild "$Configuration/ltr_d3d9_real_bridge_sink.exe"
    if (-not (Test-Path $bridgeSink)) { throw "Real bridge sink was not produced: $bridgeSink" }
    if ((Get-PeMachine $bridgeSink) -ne 0x8664) { throw "Real bridge sink is not x64" }
}

$proxy = Join-Path $build "d3d9-real-observer-forwarder/d3dx9_29.dll"
$bootstrap = Join-Path $build "$Configuration/ltr_d3d9_real_observer_bootstrap.dll"
if (-not (Test-Path $proxy)) { throw "Observer forwarder was not produced: $proxy" }
if (-not (Test-Path $bootstrap)) { throw "Observer bootstrap was not produced: $bootstrap" }
if ((Get-PeMachine $proxy) -ne 0x14C) { throw "Observer forwarder is not x86" }
if ((Get-PeMachine $bootstrap) -ne 0x14C) { throw "Observer bootstrap is not x86" }

$runtimeCandidates = @(
    (Join-Path $env:WINDIR "SysWOW64/d3dx9_29.dll"),
    (Join-Path $env:WINDIR "System32/d3dx9_29.dll")
)
$realRuntime = $runtimeCandidates |
    Where-Object { (Test-Path $_) -and ((Get-PeMachine $_) -eq 0x14C) } |
    Select-Object -First 1
if (-not $realRuntime) { throw "No x86 d3dx9_29.dll runtime was found" }

$proxyHash = (Get-FileHash $proxy -Algorithm SHA256).Hash
$realHash = (Get-FileHash $realRuntime -Algorithm SHA256).Hash
$bootstrapHash = (Get-FileHash $bootstrap -Algorithm SHA256).Hash

if ($CleanupOnly) {
    Remove-ProbeFile $stageProxy $proxyHash
    Remove-ProbeFile $stageReal $realHash
    Remove-ProbeFile $stageBootstrap $bootstrapHash
    Write-Output "cleanup=completed"
    return
}

if (Test-Path $stageProxy) { throw "Refusing to overwrite existing $stageProxy" }
if (Test-Path $stageReal) { throw "Refusing to overwrite existing $stageReal" }
if (Test-Path $stageBootstrap) { throw "Refusing to overwrite existing $stageBootstrap" }

$started = Get-Date
$log = $null
$previousPromotion = [Environment]::GetEnvironmentVariable("LTR_D3D9_PROMOTE_EX", "Process")
$previousRelayTest = [Environment]::GetEnvironmentVariable("LTR_D3D9_TEST_RELAY", "Process")
$previousBridgeTest = [Environment]::GetEnvironmentVariable("LTR_D3D9_TEST_X86_X64", "Process")
$previousMultiframeTest = [Environment]::GetEnvironmentVariable("LTR_D3D9_TEST_MULTIFRAME", "Process")
$previousMultiframeStall = [Environment]::GetEnvironmentVariable("LTR_D3D9_MULTIFRAME_STALL_MS", "Process")
$previousBridgeSink = [Environment]::GetEnvironmentVariable("LTR_D3D9_REAL_BRIDGE_SINK", "Process")

try {
    if ($PromoteToD3D9Ex) {
        [Environment]::SetEnvironmentVariable("LTR_D3D9_PROMOTE_EX", "1", "Process")
    }
    else {
        [Environment]::SetEnvironmentVariable("LTR_D3D9_PROMOTE_EX", $null, "Process")
    }
    if ($TestSharedRelay) {
        [Environment]::SetEnvironmentVariable("LTR_D3D9_TEST_RELAY", "1", "Process")
    }
    else {
        [Environment]::SetEnvironmentVariable("LTR_D3D9_TEST_RELAY", $null, "Process")
    }
    if ($TestX86X64Bridge) {
        [Environment]::SetEnvironmentVariable("LTR_D3D9_TEST_X86_X64", "1", "Process")
    }
    else {
        [Environment]::SetEnvironmentVariable("LTR_D3D9_TEST_X86_X64", $null, "Process")
    }
    if ($TestMultiframeBridge) {
        [Environment]::SetEnvironmentVariable("LTR_D3D9_TEST_MULTIFRAME", "1", "Process")
    }
    else {
        [Environment]::SetEnvironmentVariable("LTR_D3D9_TEST_MULTIFRAME", $null, "Process")
    }
    if ($RequireResetGeneration -and $ResetBridgeStallMs -gt 0) {
        [Environment]::SetEnvironmentVariable("LTR_D3D9_MULTIFRAME_STALL_MS", [string]$ResetBridgeStallMs, "Process")
    }
    else {
        [Environment]::SetEnvironmentVariable("LTR_D3D9_MULTIFRAME_STALL_MS", $null, "Process")
    }
    if ($TestX86X64Bridge -or $TestMultiframeBridge) {
        [Environment]::SetEnvironmentVariable("LTR_D3D9_REAL_BRIDGE_SINK", $bridgeSink, "Process")
    }
    else {
        [Environment]::SetEnvironmentVariable("LTR_D3D9_REAL_BRIDGE_SINK", $null, "Process")
    }
    Copy-Item $realRuntime $stageReal
    Copy-Item $bootstrap $stageBootstrap
    Copy-Item $proxy $stageProxy
    [void](Start-Process -FilePath $gameExePath -WorkingDirectory $gameDir -PassThru)

    $effectiveTimeoutSeconds = if ($RequireResetGeneration) {
        [Math]::Max($TimeoutSeconds, $ResetTimeoutSeconds)
    }
    else {
        $TimeoutSeconds
    }
    $deadline = (Get-Date).AddSeconds($effectiveTimeoutSeconds)
    while ((Get-Date) -lt $deadline) {
        $candidate = Get-ChildItem $env:TEMP -Filter "ltr_d3d9_real_observer_*.log" -ErrorAction SilentlyContinue |
            Where-Object { $_.LastWriteTime -ge $started } |
            Sort-Object LastWriteTime -Descending |
            Select-Object -First 1
        if ($candidate) {
            $log = $candidate
            $text = Get-Content $candidate.FullName -Raw -ErrorAction SilentlyContinue
            if ($TestMultiframeBridge) {
                if ($RequireResetGeneration) {
                    $resetMatches = [regex]::Matches($text, "event=reset .*resource_generation=(\d+)")
                    $resetGeneration = if ($resetMatches.Count) { [int]$resetMatches[$resetMatches.Count - 1].Groups[1].Value } else { 0 }
                    if ($resetGeneration -gt 1 -and
                        $text -match "event=real_multiframe_bridge stage=shutdown reason=reset_begin .*generation=\d+" -and
                        $text -match "event=real_multiframe_bridge stage=completed .*generation=$resetGeneration .*RESULT PASS" -and
                        $text -match "event=real_multiframe_client generation=$resetGeneration frames_submitted=12 .*RESULT PASS" -and
                        $text -match "event=x64_real_bridge_multiframe stage=completion .*generation=$resetGeneration frames=12 consumer_copies=12 .*RESULT PASS") {
                        break
                    }
                }
                elseif ($text -match "event=real_multiframe_bridge stage=completed .*RESULT PASS") { break }
            }
            elseif ($text -match "OBSERVATION_RESULT OBSERVED") {
                break
            }
        }
        Start-Sleep -Milliseconds 250
    }

    if (-not $log) { throw "No observer log was produced" }
    $text = Get-Content $log.FullName -Raw
    $text
    if ($text -notmatch "event=observer_installed .*engine_get_proc_address_hook=1" -or
        $text -notmatch "event=direct3dcreate9_resolved interception=1" -or
        $text -notmatch "event=device_created" -or
        $text -notmatch "event=first_state_sample") {
        throw "D3D9 device/render-state observation was not reached"
    }
    if ($PromoteToD3D9Ex -and
        ($text -notmatch "event=direct3d9_created .*promotion_requested=1 promoted_ex=1" -or
         $text -notmatch "event=device_created .*device_ex=1")) {
        throw "D3D9Ex runtime promotion was requested but not observed"
    }
    if ($TestMultiframeBridge -and
        $text -notmatch "event=first_state_sample .*rt_observed=1 .*depth_observed=1 .*transforms_observed=1") {
        throw "Multiframe run did not observe color, depth, and transforms at the real Present boundary"
    }
    if ($TestSharedRelay -and $text -notmatch "event=real_relay_test .*RESULT PASS") {
        throw "Real-frame shared relay test did not pass"
    }
    if ($TestX86X64Bridge -and
        ($text -notmatch "event=x64_real_bridge_sink .*RESULT PASS" -or
         $text -notmatch "event=real_x86_x64_bridge .*RESULT PASS")) {
        throw "Real-frame x86/x64 bridge test did not pass"
    }
    if ($TestMultiframeBridge -and
        ($text -notmatch "event=x64_real_bridge_multiframe stage=completion .*frames=12 .*RESULT PASS" -or
         $text -notmatch "event=real_multiframe_client .*frames_submitted=12 .*RESULT PASS" -or
         $text -notmatch "event=real_multiframe_bridge stage=completed .*RESULT PASS")) {
        throw "Real-frame multiframe bridge test did not pass all completion criteria"
    }
    if ($TestMultiframeBridge -and $text -notmatch "event=x64_real_bridge_multiframe stage=completion .*consumer_copies=12 .*RESULT PASS") {
        throw "Real-frame multiframe bridge did not execute the x64 D3D12 consumer copy for all frames"
    }
    if ($RequireResetGeneration) {
        $resetMatches = [regex]::Matches($text, "event=reset .*resource_generation=(\d+)")
        $resetGeneration = if ($resetMatches.Count) { [int]$resetMatches[$resetMatches.Count - 1].Groups[1].Value } else { 0 }
        if ($resetGeneration -le 1 -or
            $text -notmatch "event=real_multiframe_bridge stage=shutdown reason=reset_begin .*generation=\d+" -or
            $text -notmatch "event=first_state_sample .*resource_generation=$resetGeneration .*rt_observed=1 .*depth_observed=1 .*transforms_observed=1" -or
            $text -notmatch "event=real_multiframe_bridge stage=completed .*generation=$resetGeneration .*RESULT PASS" -or
            $text -notmatch "event=real_multiframe_client generation=$resetGeneration frames_submitted=12 .*RESULT PASS" -or
            $text -notmatch "event=x64_real_bridge_multiframe stage=completion .*generation=$resetGeneration frames=12 consumer_copies=12 .*RESULT PASS") {
            throw "Reset-generation validation requires an active-bridge reset teardown plus one fully completed post-reset generation across observer, client, and x64 sink"
        }
        Write-Output ("reset_generation_validation=passed generation={0}" -f $resetGeneration)
    }
}
finally {
    $launched = @(Get-Process CoJ -ErrorAction SilentlyContinue | Where-Object {
        try { $_.StartTime -ge $started.AddSeconds(-2) } catch { $false }
    })
    foreach ($process in $launched) {
        Stop-Process -Id $process.Id -Force -ErrorAction SilentlyContinue
    }
    foreach ($process in $launched) {
        try { Wait-Process -Id $process.Id -Timeout 5 -ErrorAction SilentlyContinue } catch {}
    }
    Remove-ProbeFile $stageProxy $proxyHash
    Remove-ProbeFile $stageReal $realHash
    Remove-ProbeFile $stageBootstrap $bootstrapHash
    [Environment]::SetEnvironmentVariable("LTR_D3D9_PROMOTE_EX", $previousPromotion, "Process")
    [Environment]::SetEnvironmentVariable("LTR_D3D9_TEST_RELAY", $previousRelayTest, "Process")
    [Environment]::SetEnvironmentVariable("LTR_D3D9_TEST_X86_X64", $previousBridgeTest, "Process")
    [Environment]::SetEnvironmentVariable("LTR_D3D9_TEST_MULTIFRAME", $previousMultiframeTest, "Process")
    [Environment]::SetEnvironmentVariable("LTR_D3D9_MULTIFRAME_STALL_MS", $previousMultiframeStall, "Process")
    [Environment]::SetEnvironmentVariable("LTR_D3D9_REAL_BRIDGE_SINK", $previousBridgeSink, "Process")
}
