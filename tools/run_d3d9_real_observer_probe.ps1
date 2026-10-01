param(
    [Parameter(Mandatory = $true)]
    [string]$GameExe,
    [int]$TimeoutSeconds = 20,
    [string]$Configuration = "Release",
    [switch]$PromoteToD3D9Ex,
    [switch]$ManagedSemanticAdaptation,
    [switch]$TraceExceptions,
    [switch]$TraceChromeFlow,
    [switch]$ManagedTextureFallback,
    [switch]$ManagedVertexBufferFallback,
    [switch]$ManagedIndexBufferFallback,
    [switch]$CanonicalVertexBufferHook,
    [switch]$BeginStateBlockVertexBufferRehook,
    [switch]$RearmChromeBeginBreakpoints,
    [switch]$ObserveManagedVertexBufferStability,
    [switch]$ResourceCensus,
    [switch]$TestSharedRelay,
    [switch]$TestX86X64Bridge,
    [switch]$TestMultiframeBridge,
    [switch]$RequireResetGeneration,
    [int]$ResetTimeoutSeconds = 90,
    [int]$ResetBridgeStallMs = 15000,
    [switch]$CleanupOnly
)

$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot "d3d9_real_observer_validation.ps1")
$repoRoot = Split-Path -Parent $PSScriptRoot
$gameExePath = (Resolve-Path $GameExe).Path
$gameDir = Split-Path -Parent $gameExePath
$build = Join-Path $repoRoot "build-root/d3d9-real-observer-x86"
$bridgeBuild = Join-Path $repoRoot "build-root/d3d9-real-bridge-x64"
$stageProxy = Join-Path $gameDir "d3dx9_29.dll"
$stageReal = Join-Path $gameDir "ltr_d3dx9_29_real.dll"
$stageBootstrap = Join-Path $gameDir "ltr_d3d9_real_observer_bootstrap.dll"

if ($ResourceCensus -and ($PromoteToD3D9Ex -or $ManagedTextureFallback -or
    $ManagedVertexBufferFallback -or $ManagedIndexBufferFallback -or
    $TraceChromeFlow -or $TestSharedRelay -or $TestX86X64Bridge -or $TestMultiframeBridge)) {
    throw "ResourceCensus requires an unmodified classic-D3D9 source route"
}
if ($ResourceCensus -and ($TimeoutSeconds -lt 1 -or $TimeoutSeconds -gt 120)) {
    throw "ResourceCensus requires a bounded observation window of 1 to 120 seconds"
}

if ($TestMultiframeBridge -and -not $PromoteToD3D9Ex) {
    throw "TestMultiframeBridge requires PromoteToD3D9Ex"
}
if ($ManagedSemanticAdaptation -and -not $PromoteToD3D9Ex) {
    throw "ManagedSemanticAdaptation requires PromoteToD3D9Ex"
}
if ($ManagedSemanticAdaptation -and ($ManagedTextureFallback -or
    $ManagedVertexBufferFallback -or $ManagedIndexBufferFallback)) {
    throw "ManagedSemanticAdaptation cannot be combined with the size-specific managed fallbacks"
}
if ($ManagedTextureFallback -and -not $PromoteToD3D9Ex) {
    throw "ManagedTextureFallback requires PromoteToD3D9Ex"
}
if ($ManagedVertexBufferFallback -and -not $PromoteToD3D9Ex) {
    throw "ManagedVertexBufferFallback requires PromoteToD3D9Ex"
}
if ($ManagedVertexBufferFallback -and -not $TraceChromeFlow) {
    throw "ManagedVertexBufferFallback requires TraceChromeFlow so the exact ChromeEngine3 build is verified"
}
if ($ManagedIndexBufferFallback -and -not $ManagedVertexBufferFallback) {
    throw "ManagedIndexBufferFallback requires ManagedVertexBufferFallback so the exact observed startup sequence is exercised"
}
if ($ManagedIndexBufferFallback -and -not $BeginStateBlockVertexBufferRehook) {
    throw "ManagedIndexBufferFallback requires BeginStateBlockVertexBufferRehook so the index-buffer hook survives D3D9 state-block vtable refreshes"
}
if ($ObserveManagedVertexBufferStability -and -not $ManagedVertexBufferFallback) {
    throw "ObserveManagedVertexBufferStability requires ManagedVertexBufferFallback"
}
if ($RearmChromeBeginBreakpoints -and -not $TraceChromeFlow) {
    throw "RearmChromeBeginBreakpoints requires TraceChromeFlow"
}
if ($CanonicalVertexBufferHook -and -not $ManagedVertexBufferFallback) {
    throw "CanonicalVertexBufferHook requires ManagedVertexBufferFallback"
}
if ($BeginStateBlockVertexBufferRehook -and -not $ManagedVertexBufferFallback) {
    throw "BeginStateBlockVertexBufferRehook requires ManagedVertexBufferFallback"
}
if ($BeginStateBlockVertexBufferRehook -and $CanonicalVertexBufferHook) {
    throw "BeginStateBlockVertexBufferRehook and CanonicalVertexBufferHook are separate experiments and cannot be combined"
}
if ($BeginStateBlockVertexBufferRehook -and $RearmChromeBeginBreakpoints) {
    throw "BeginStateBlockVertexBufferRehook must not use reusable Chrome software-breakpoint rearming"
}
if ($TraceChromeFlow -and -not $TraceExceptions) {
    throw "TraceChromeFlow requires TraceExceptions"
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
if ($TraceChromeFlow -or $ResourceCensus -or $ManagedSemanticAdaptation) {
    $chromeEngine = Join-Path $gameDir "ChromeEngine3.dll"
    $expectedChromeEngineHash = "DB69BC35919FE57187766771A2452ACA11090474F6D63DF1A85A80EDED131EC8"
    if (-not (Test-Path $chromeEngine)) {
        throw "Chrome flow/census requires ChromeEngine3.dll next to the game executable"
    }
    $chromeEngineHash = (Get-FileHash $chromeEngine -Algorithm SHA256).Hash
    if ($chromeEngineHash -ne $expectedChromeEngineHash) {
        throw "Chrome flow/census is pinned to ChromeEngine3.dll SHA-256 $expectedChromeEngineHash; observed $chromeEngineHash"
    }
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
$previousManagedSemanticAdaptation = [Environment]::GetEnvironmentVariable("LTR_D3D9_MANAGED_SEMANTIC_ADAPTATION", "Process")
$previousResourceCensus = [Environment]::GetEnvironmentVariable("LTR_D3D9_RESOURCE_CENSUS", "Process")
$previousTraceExceptions = [Environment]::GetEnvironmentVariable("LTR_D3D9_TRACE_EXCEPTIONS", "Process")
$previousTraceChromeFlow = [Environment]::GetEnvironmentVariable("LTR_D3D9_TRACE_CHROME_FLOW", "Process")
$previousManagedTextureFallback = [Environment]::GetEnvironmentVariable("LTR_D3D9_MANAGED_TEXTURE_FALLBACK", "Process")
$previousManagedVertexBufferFallback = [Environment]::GetEnvironmentVariable("LTR_D3D9_MANAGED_VERTEX_BUFFER_FALLBACK", "Process")
$previousManagedIndexBufferFallback = [Environment]::GetEnvironmentVariable("LTR_D3D9_MANAGED_INDEX_BUFFER_FALLBACK", "Process")
$previousCanonicalVertexBufferHook = [Environment]::GetEnvironmentVariable("LTR_D3D9_CANONICAL_VB_HOOK", "Process")
$previousBeginStateBlockVertexBufferRehook = [Environment]::GetEnvironmentVariable("LTR_D3D9_BEGIN_STATE_BLOCK_VB_REHOOK", "Process")
$previousRearmChromeBeginBreakpoints = [Environment]::GetEnvironmentVariable("LTR_D3D9_REARM_CHROME_BEGIN_BREAKPOINTS", "Process")
$previousRelayTest = [Environment]::GetEnvironmentVariable("LTR_D3D9_TEST_RELAY", "Process")
$previousBridgeTest = [Environment]::GetEnvironmentVariable("LTR_D3D9_TEST_X86_X64", "Process")
$previousMultiframeTest = [Environment]::GetEnvironmentVariable("LTR_D3D9_TEST_MULTIFRAME", "Process")
$previousMultiframeStall = [Environment]::GetEnvironmentVariable("LTR_D3D9_MULTIFRAME_STALL_MS", "Process")
$previousBridgeSink = [Environment]::GetEnvironmentVariable("LTR_D3D9_REAL_BRIDGE_SINK", "Process")

try {
    [Environment]::SetEnvironmentVariable("LTR_D3D9_RESOURCE_CENSUS", $(if ($ResourceCensus) { "1" } else { $null }), "Process")
    [Environment]::SetEnvironmentVariable("LTR_D3D9_MANAGED_SEMANTIC_ADAPTATION", $(if ($ManagedSemanticAdaptation) { "1" } else { $null }), "Process")
    if ($PromoteToD3D9Ex) {
        [Environment]::SetEnvironmentVariable("LTR_D3D9_PROMOTE_EX", "1", "Process")
    }
    else {
        [Environment]::SetEnvironmentVariable("LTR_D3D9_PROMOTE_EX", $null, "Process")
    }
    if ($TraceExceptions) {
        [Environment]::SetEnvironmentVariable("LTR_D3D9_TRACE_EXCEPTIONS", "1", "Process")
    }
    else {
        [Environment]::SetEnvironmentVariable("LTR_D3D9_TRACE_EXCEPTIONS", $null, "Process")
    }
    if ($TraceChromeFlow) {
        [Environment]::SetEnvironmentVariable("LTR_D3D9_TRACE_CHROME_FLOW", "1", "Process")
    }
    else {
        [Environment]::SetEnvironmentVariable("LTR_D3D9_TRACE_CHROME_FLOW", $null, "Process")
    }
    if ($ManagedTextureFallback) {
        [Environment]::SetEnvironmentVariable("LTR_D3D9_MANAGED_TEXTURE_FALLBACK", "1", "Process")
    }
    else {
        [Environment]::SetEnvironmentVariable("LTR_D3D9_MANAGED_TEXTURE_FALLBACK", $null, "Process")
    }
    if ($ManagedVertexBufferFallback) {
        [Environment]::SetEnvironmentVariable("LTR_D3D9_MANAGED_VERTEX_BUFFER_FALLBACK", "1", "Process")
    }
    else {
        [Environment]::SetEnvironmentVariable("LTR_D3D9_MANAGED_VERTEX_BUFFER_FALLBACK", $null, "Process")
    }
    if ($ManagedIndexBufferFallback) {
        [Environment]::SetEnvironmentVariable("LTR_D3D9_MANAGED_INDEX_BUFFER_FALLBACK", "1", "Process")
    }
    else {
        [Environment]::SetEnvironmentVariable("LTR_D3D9_MANAGED_INDEX_BUFFER_FALLBACK", $null, "Process")
    }
    if ($CanonicalVertexBufferHook) {
        [Environment]::SetEnvironmentVariable("LTR_D3D9_CANONICAL_VB_HOOK", "1", "Process")
    }
    else {
        [Environment]::SetEnvironmentVariable("LTR_D3D9_CANONICAL_VB_HOOK", $null, "Process")
    }
    if ($BeginStateBlockVertexBufferRehook) {
        [Environment]::SetEnvironmentVariable("LTR_D3D9_BEGIN_STATE_BLOCK_VB_REHOOK", "1", "Process")
    }
    else {
        [Environment]::SetEnvironmentVariable("LTR_D3D9_BEGIN_STATE_BLOCK_VB_REHOOK", $null, "Process")
    }
    if ($RearmChromeBeginBreakpoints) {
        [Environment]::SetEnvironmentVariable("LTR_D3D9_REARM_CHROME_BEGIN_BREAKPOINTS", "1", "Process")
    }
    else {
        [Environment]::SetEnvironmentVariable("LTR_D3D9_REARM_CHROME_BEGIN_BREAKPOINTS", $null, "Process")
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
            # Concurrent appends can yield more than one raw chunk on Windows
            # PowerShell. Keep regex acceptance scalar rather than array filtering.
            $text = (Get-Content $candidate.FullName -Raw -ErrorAction SilentlyContinue) -join ''
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
            elseif ($TraceChromeFlow -and $ManagedVertexBufferFallback -and
                    -not $ObserveManagedVertexBufferStability -and
                    $text -match "event=managed_vertex_buffer_unlock ordinal=8 .*hr=0x0") {
                break
            }
            elseif ($TraceChromeFlow -and -not $ManagedVertexBufferFallback -and
                    $text -match "event=device_vb_slot_write_watch ") {
                break
            }
            elseif ($TraceExceptions -and $text -match "event=exception_trace .*code=0xc0000005") {
                break
            }
            elseif (-not $ResourceCensus -and $text -match "OBSERVATION_RESULT OBSERVED") {
                break
            }
        }
        Start-Sleep -Milliseconds 250
    }

    if (-not $log) { throw "No observer log was produced" }
    $text = (Get-Content $log.FullName -Raw) -join ''
    $text
    Write-Output ("observer_log={0}" -f $log.FullName)
    if ($ResourceCensus) {
        if ($text -notmatch "event=device_created .*device_ex=0 .*texture_trace_hook=1 volume_texture_trace_hook=1 cube_texture_trace_hook=1 vertex_buffer_trace_hook=1 index_buffer_trace_hook=1 .*begin_state_block_rehook_hook=1" -or
            $text -notmatch "event=summary .*present=[1-9][0-9]{2,} .*OBSERVATION_RESULT OBSERVED" -or
            $text -notmatch "event=begin_state_block_rehook .*observer_hooks=1" -or
            $text -notmatch "event=create_texture_trace .*pool=1 .*final_hr=0x0 .*texture=non_null" -or
            $text -notmatch "event=create_cube_texture_trace .*pool=1 .*final_hr=0x0 .*texture=non_null" -or
            $text -notmatch "event=create_vertex_buffer_trace .*pool=1 .*final_hr=0x0 .*buffer=non_null" -or
            $text -notmatch "event=create_index_buffer_trace .*pool=1 .*final_hr=0x0 .*buffer=non_null" -or
            $text -notmatch "event=managed_census_use_trace kind=texture2d .*lock_hook=1 unlock_hook=1" -or
            $text -notmatch "event=managed_census_use_trace kind=cube_texture .*lock_hook=1 unlock_hook=1" -or
            $text -notmatch "event=managed_census_use_trace kind=vertex_buffer .*hooks=1" -or
            $text -notmatch "event=managed_census_use_trace kind=index_buffer .*lock_hook=1 unlock_hook=1" -or
            $text -match "event=exception_trace .*code=0xc0000005" -or
            $text -match "event=create_.*_trace .*fallback_default=1" -or
            $text -match "event=create_(texture|cube_texture|volume_texture|vertex_buffer|index_buffer)_trace .* (final_hr|hr)=0x[1-9a-f][0-9a-f]*") {
            throw "Classic-D3D9 census requires sustained Present, preserved creation/semantic hooks, successful MANAGED textures/cube/VB/IB and no allocation mutation or failure"
        }
        Write-Output "classic_d3d9_resource_census=passed scope=observed_startup_window"
    }
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
    if ($TraceExceptions -and -not $ResourceCensus -and -not $ManagedVertexBufferFallback -and
        -not $ManagedSemanticAdaptation -and
        $text -notmatch "event=exception_trace .*code=0xc0000005") {
        throw "Exception tracing was requested but no access violation was observed"
    }
    if ($ManagedSemanticAdaptation) {
        $beginStateBlockRehookMatches = [regex]::Matches($text, "event=begin_state_block_rehook .*observer_hooks=1")
        $managedResetGeneration = Get-LtrResetGeneration -Text $text
        if ($text -notmatch "event=device_created .*device_ex=1 .*texture_trace_hook=1 volume_texture_trace_hook=1 cube_texture_trace_hook=1 vertex_buffer_trace_hook=1 index_buffer_trace_hook=1 .*begin_state_block_rehook_hook=1" -or
            $beginStateBlockRehookMatches.Count -lt 1 -or
            $text -notmatch "event=create_texture_trace .*pool=1 .*original_hr=0x8876086c final_hr=0x0 .*semantic_adaptation=1 .*retry_usage=0x200 retry_pool=0 .*census_resource_id=[1-9][0-9]* .*texture=non_null" -or
            $text -notmatch "event=create_cube_texture_trace .*pool=1 .*original_hr=0x8876086c final_hr=0x0 .*semantic_adaptation=1 .*retry_usage=0x200 retry_pool=0 .*census_resource_id=[1-9][0-9]* .*texture=non_null" -or
            $text -notmatch "event=create_vertex_buffer_trace .*pool=1 .*original_hr=0x8876086c final_hr=0x0 .*semantic_adaptation=1 .*retry_usage=0x8 retry_pool=0 .*census_resource_id=[1-9][0-9]* .*buffer=non_null" -or
            $text -notmatch "event=create_index_buffer_trace .*pool=1 .*original_hr=0x8876086c final_hr=0x0 .*semantic_adaptation=1 .*retry_usage=0x8 retry_pool=0 .*census_resource_id=[1-9][0-9]* .*buffer=non_null" -or
            $text -notmatch "event=managed_census_surface_lock .*parent_kind=texture2d .*flags=0x800 hr=0x0 .*data=non_null" -or
            $text -notmatch "event=managed_census_surface_lock .*parent_kind=cube_texture .*flags=0x800 hr=0x0 .*data=non_null" -or
            $text -notmatch "event=managed_census_vertex_buffer_lock .*flags=0x0 hr=0x0 data=non_null" -or
            $text -notmatch "event=managed_census_index_buffer_lock .*flags=0x0 hr=0x0 data=non_null" -or
            -not (Test-LtrManagedSemanticCompletion -Text $text -RequireResetGeneration:$RequireResetGeneration -ResetGeneration $managedResetGeneration) -or
            $text -match "event=exception_trace .*code=0xc0000005") {
            throw "Managed semantic adaptation did not preserve the observed Chrome MANAGED creation/write semantics through sustained D3D9Ex rendering"
        }
        Write-Output ("managed_semantic_adaptation_validation=passed rehooks={0}" -f $beginStateBlockRehookMatches.Count)
    }
    if ($ManagedVertexBufferFallback) {
        if ($BeginStateBlockVertexBufferRehook) {
            $beginStateBlockRehookMatches = [regex]::Matches($text, "event=begin_state_block_rehook .*vertex_buffer=1 begin_state_block=1")
            if ($text -notmatch "event=managed_vertex_buffer_fallback armed=1 mechanism=device_begin_state_block_hook" -or
                $text -notmatch "event=device_created .*begin_state_block_rehook_hook=1" -or
                $beginStateBlockRehookMatches.Count -lt 3 -or
                $text -notmatch "event=create_vertex_buffer_trace .*length=262144 .*usage=0x8 fvf=0x0 .*pool=1 .*original_hr=0x8876086c final_hr=0x0 .*fallback_default=1" -or
                $text -notmatch "event=managed_vertex_buffer_unlock ordinal=8 .*hr=0x0") {
                throw "BeginStateBlock vertex-buffer rehook did not survive repeated state-block refreshes and exercise the exact Chrome startup buffer"
            }
            Write-Output ("begin_state_block_vertex_buffer_rehook_validation=passed rehooks={0}" -f $beginStateBlockRehookMatches.Count)
            if ($ManagedIndexBufferFallback -and
                ($text -notmatch "event=managed_index_buffer_fallback armed=1 .*length=0x20000 usage=0x8 format=101 pool_from=1 pool_to=0 shared_handle=0" -or
                 $text -notmatch "event=create_index_buffer_trace .*length=131072 .*usage=0x8 .*format=101 .*format_hex=0x65 .*pool=1 .*original_hr=0x8876086c final_hr=0x0 .*fallback_default=1 .*match_length=1 .*match_usage=1 .*match_format=1 .*match_pool=1 .*match_shared_handle=1 .*match_hr=1 .*buffer=non_null")) {
                throw "Managed index-buffer fallback did not translate the exact observed Chrome startup buffer from MANAGED to DEFAULT"
            }
            if ($ManagedTextureFallback -and $ManagedIndexBufferFallback -and
                $text -notmatch "event=create_texture_trace .*width=64 height=64 levels=0 usage=0x0 .*format=827611204 format_hex=0x31545844 .*pool=1 .*original_hr=0x8876086c final_hr=0x0 .*fallback_default=1 .*texture=non_null") {
                throw "Managed texture fallback did not translate the exact observed 64x64 DXT1 Chrome startup texture from MANAGED to DEFAULT"
            }
            if ($ObserveManagedVertexBufferStability) {
                if ($text -notmatch "event=summary .*present=([1-9][0-9]{2,}|[1-9][0-9]{3,}) .*OBSERVATION_RESULT OBSERVED") {
                    throw "BeginStateBlock rehook stability requires continued Present observation through the normal summary boundary"
                }
                Write-Output "begin_state_block_vertex_buffer_rehook_stability=passed"
            }
        }
        elseif ($text -notmatch "event=managed_vertex_buffer_fallback armed=1 mechanism=post_begin_state_block_rehook" -or
                $text -notmatch "event=state_block_vb_rehook point=after_begin_state_block .*installed=1" -or
                $text -notmatch "event=state_block_vb_rehook point=after_second_begin_state_block .*installed=1" -or
                $text -notmatch "event=state_block_vb_rehook point=after_third_begin_state_block .*installed=1" -or
                $text -notmatch "event=create_vertex_buffer_trace .*length=262144 .*usage=0x8 fvf=0x0 .*pool=1 .*original_hr=0x8876086c final_hr=0x0 .*fallback_default=1" -or
                $text -notmatch "event=managed_vertex_buffer_unlock ordinal=8 .*hr=0x0") {
            throw "Managed vertex-buffer rehook fallback did not intercept, translate, and exercise the exact Chrome startup buffer"
        }
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
        $resetGeneration = Get-LtrResetGeneration -Text $text
        if (-not (Test-LtrResetGenerationEvidence -Text $text -Generation $resetGeneration)) {
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
    [Environment]::SetEnvironmentVariable("LTR_D3D9_MANAGED_SEMANTIC_ADAPTATION", $previousManagedSemanticAdaptation, "Process")
    [Environment]::SetEnvironmentVariable("LTR_D3D9_RESOURCE_CENSUS", $previousResourceCensus, "Process")
    [Environment]::SetEnvironmentVariable("LTR_D3D9_TRACE_EXCEPTIONS", $previousTraceExceptions, "Process")
    [Environment]::SetEnvironmentVariable("LTR_D3D9_TRACE_CHROME_FLOW", $previousTraceChromeFlow, "Process")
    [Environment]::SetEnvironmentVariable("LTR_D3D9_MANAGED_TEXTURE_FALLBACK", $previousManagedTextureFallback, "Process")
    [Environment]::SetEnvironmentVariable("LTR_D3D9_MANAGED_VERTEX_BUFFER_FALLBACK", $previousManagedVertexBufferFallback, "Process")
    [Environment]::SetEnvironmentVariable("LTR_D3D9_MANAGED_INDEX_BUFFER_FALLBACK", $previousManagedIndexBufferFallback, "Process")
    [Environment]::SetEnvironmentVariable("LTR_D3D9_CANONICAL_VB_HOOK", $previousCanonicalVertexBufferHook, "Process")
    [Environment]::SetEnvironmentVariable("LTR_D3D9_BEGIN_STATE_BLOCK_VB_REHOOK", $previousBeginStateBlockVertexBufferRehook, "Process")
    [Environment]::SetEnvironmentVariable("LTR_D3D9_REARM_CHROME_BEGIN_BREAKPOINTS", $previousRearmChromeBeginBreakpoints, "Process")
    [Environment]::SetEnvironmentVariable("LTR_D3D9_TEST_RELAY", $previousRelayTest, "Process")
    [Environment]::SetEnvironmentVariable("LTR_D3D9_TEST_X86_X64", $previousBridgeTest, "Process")
    [Environment]::SetEnvironmentVariable("LTR_D3D9_TEST_MULTIFRAME", $previousMultiframeTest, "Process")
    [Environment]::SetEnvironmentVariable("LTR_D3D9_MULTIFRAME_STALL_MS", $previousMultiframeStall, "Process")
    [Environment]::SetEnvironmentVariable("LTR_D3D9_REAL_BRIDGE_SINK", $previousBridgeSink, "Process")
}
