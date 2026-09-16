param(
    [string]$Configuration = "Release"
)

$ErrorActionPreference = "Stop"
$repoRoot = Split-Path -Parent $PSScriptRoot
$build = Join-Path $repoRoot "build-root/d3d9ex-intercept-x86"
$stage = Join-Path $build ("stage-interceptor-" + [guid]::NewGuid().ToString("N"))

& cmake -S $repoRoot -B $build -A Win32 -DLTR_ENABLE_D3D9EX_INTERCEPT_PROBE=ON
if ($LASTEXITCODE -ne 0) { throw "CMake configure failed: $LASTEXITCODE" }
& cmake --build $build --config $Configuration --target ltr_d3d9ex_intercept_target
if ($LASTEXITCODE -ne 0) { throw "Target build failed: $LASTEXITCODE" }
& cmake --build $build --config $Configuration --target ltr_d3d9ex_interceptor
if ($LASTEXITCODE -ne 0) { throw "Interceptor build failed: $LASTEXITCODE" }

$target = Join-Path $build "target-bin/$Configuration/ltr_d3d9ex_intercept_target.exe"
$interceptor = Join-Path $build "interceptor-bin/$Configuration/ltr_d3d9ex_interceptor.dll"
if (-not (Test-Path $target)) { throw "D3D9Ex interception target was not produced" }
if (-not (Test-Path $interceptor)) { throw "D3D9Ex interceptor was not produced" }

New-Item -ItemType Directory -Path $stage | Out-Null
Copy-Item $target (Join-Path $stage "ltr_d3d9ex_intercept_target.exe")
Copy-Item $interceptor (Join-Path $stage "ltr_d3d9ex_interceptor.dll")

Push-Location $stage
try {
    $output = & (Join-Path $stage "ltr_d3d9ex_intercept_target.exe") 2>&1 | Tee-Object -Variable captured
    $exitCode = $LASTEXITCODE
    $output | Write-Host
    if ($exitCode -ne 0) { throw "Interception target failed: $exitCode" }
    $text = $captured -join "`n"
    if ($text -notmatch "INTERCEPT_RESULT PASS") { throw "Interceptor result was not observed" }
    if ($text -notmatch "target_frames=12 target_resets=1 RESULT PASS") { throw "Target result was not observed" }
} finally {
    Pop-Location
    Remove-Item $stage -Recurse -Force -ErrorAction SilentlyContinue
}
