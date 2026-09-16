param(
    [string]$Configuration = "Release",
    [string]$DgVoodooRoot = $env:LTR_DGVOODOO_ROOT,
    [string]$DgVoodooApiRoot = $env:LTR_DGVOODOO_API_ROOT
)

$ErrorActionPreference = "Stop"
$repoRoot = Split-Path -Parent $PSScriptRoot

if (-not $DgVoodooRoot) {
    $localPinned = Join-Path $repoRoot "build-root/external/dgvoodoo2-2.87.5/package"
    if (Test-Path (Join-Path $localPinned "MS/x86/D3D9.dll")) {
        $DgVoodooRoot = $localPinned
    }
}
if (-not $DgVoodooApiRoot) {
    $localPinnedApi = Join-Path $repoRoot "build-root/external/dgvoodoo2-2.87.5-api/package"
    if (Test-Path (Join-Path $localPinnedApi "Inc/Addon/ID3D12RootObserver.hpp")) {
        $DgVoodooApiRoot = $localPinnedApi
    }
}

$wrapper = if ($DgVoodooRoot) { Join-Path $DgVoodooRoot "MS/x86/D3D9.dll" } else { "" }
$config = if ($DgVoodooRoot) { Join-Path $DgVoodooRoot "dgVoodoo.conf" } else { "" }
$addonHeader = if ($DgVoodooApiRoot) { Join-Path $DgVoodooApiRoot "Inc/Addon/ID3D12RootObserver.hpp" } else { "" }
$addonLib = if ($DgVoodooApiRoot) { Join-Path $DgVoodooApiRoot "Lib/x86/dgVoodooAddon.lib" } else { "" }
foreach ($required in @($wrapper, $config, $addonHeader, $addonLib)) {
    if (-not $required -or -not (Test-Path $required)) {
        throw "Required external dgVoodoo2 2.87.5 artifact not found: $required"
    }
}

$build = Join-Path $repoRoot "build-root/dgvoodoo-addon-x86"
$stage = Join-Path $build ("stage-" + [guid]::NewGuid().ToString("N"))

& cmake -S $repoRoot -B $build -A Win32 `
    -DLTR_ENABLE_D3D9EX_INTERCEPT_PROBE=ON `
    -DLTR_ENABLE_DGVOODOO_ADDON_PROBE=ON `
    "-DLTR_DGVOODOO_API_ROOT=$DgVoodooApiRoot"
if ($LASTEXITCODE -ne 0) { throw "CMake configure failed: $LASTEXITCODE" }

& cmake --build $build --config $Configuration --target ltr_d3d9ex_intercept_target ltr_dgvoodoo_addon_probe
if ($LASTEXITCODE -ne 0) { throw "Probe build failed: $LASTEXITCODE" }

$target = Join-Path $build "target-bin/$Configuration/ltr_d3d9ex_intercept_target.exe"
$addon = Join-Path $build "dgvoodoo-addon-bin/$Configuration/SampleAddon.dll"
if (-not (Test-Path $target)) { throw "D3D9Ex comparison target was not produced" }
if (-not (Test-Path $addon)) { throw "dgVoodoo addon probe was not produced" }

New-Item -ItemType Directory -Path $stage | Out-Null
Copy-Item $target (Join-Path $stage "ltr_d3d9ex_intercept_target.exe")
Copy-Item $addon (Join-Path $stage "SampleAddon.dll")
Copy-Item $wrapper (Join-Path $stage "D3D9.dll")

$configText = Get-Content $config -Raw
$configText = $configText -replace '(?m)^OutputAPI\s*=.*$', 'OutputAPI                            = d3d12_fl11_0'
Set-Content -Path (Join-Path $stage "dgVoodoo.conf") -Value $configText -Encoding utf8

Push-Location $stage
try {
    $targetOutput = & (Join-Path $stage "ltr_d3d9ex_intercept_target.exe") --bgra8 --no-interceptor --present 2>&1 | Tee-Object -Variable captured
    $exitCode = $LASTEXITCODE
    $targetOutput | Write-Host
    if ($exitCode -ne 0) { throw "dgVoodoo comparison target failed: $exitCode" }

    $targetText = $captured -join "`n"
    if ($targetText -notmatch 'target_frames=12 target_resets=1 .*d3d11_loaded=0 d3d12_loaded=1 RESULT PASS') {
        throw "Expected D3D12-backed target result was not observed"
    }

    $logPath = Join-Path $stage "ltr_dgvoodoo_addon_probe.log"
    if (-not (Test-Path $logPath)) { throw "dgVoodoo addon probe log was not produced" }
    $log = Get-Content $logPath
    $activeResult = $log | Where-Object { $_ -match 'event=addon_exit .*RESULT PASS$' }
    $failResult = $log | Where-Object { $_ -match 'event=addon_exit .*RESULT FAIL$' }
    if ($failResult) { throw "dgVoodoo addon probe recorded a failed active lifecycle" }
    if (-not $activeResult) { throw "dgVoodoo addon probe did not observe an active D3D12 lifecycle" }
    if (($activeResult -join "`n") -notmatch 'swapchain_created=2 .*swapchain_changed=2 .*swapchain_released=2 .*present_begin=12 present_end=12 src_nonnull=12 dst_nonnull=12') {
        throw "dgVoodoo addon probe lifecycle did not match the expected 12-frame/two-generation contract"
    }

    $log | Where-Object {
        $_ -match '^event=(addon_init|root_created|adapter_begin|swapchain_created|swapchain_changed|addon_exit)'
    } | Write-Host
} finally {
    Pop-Location
    Remove-Item $stage -Recurse -Force -ErrorAction SilentlyContinue
}
