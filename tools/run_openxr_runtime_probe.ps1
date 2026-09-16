param(
    [string]$OpenXRSDKRoot = $env:LTR_OPENXR_SDK_ROOT,
    [string]$LoaderPath = ""
)

$ErrorActionPreference = "Stop"
$repo = Split-Path -Parent $PSScriptRoot

if (-not $OpenXRSDKRoot) {
    $localPinned = Join-Path $repo "build-root/external/OpenXR-SDK-1.1.63"
    if (Test-Path (Join-Path $localPinned "include/openxr/openxr.h")) {
        $OpenXRSDKRoot = $localPinned
    }
}
if (-not $OpenXRSDKRoot -or -not (Test-Path (Join-Path $OpenXRSDKRoot "include/openxr/openxr.h"))) {
    throw "OpenXR SDK headers not found. Pass -OpenXRSDKRoot or set LTR_OPENXR_SDK_ROOT."
}

if (-not $LoaderPath) {
    $activeRuntime = (Get-ItemProperty "HKLM:\SOFTWARE\Khronos\OpenXR\1" -ErrorAction SilentlyContinue).ActiveRuntime
    if ($activeRuntime) {
        $runtimeRoot = Split-Path -Parent $activeRuntime
        $runtimeLoader = Join-Path $runtimeRoot "bin/win64/openxr_loader.dll"
        if (Test-Path $runtimeLoader) {
            $LoaderPath = $runtimeLoader
        }
    }
}
if (-not $LoaderPath) {
    $systemLoader = Join-Path $env:WINDIR "System32/openxr_loader.dll"
    if (Test-Path $systemLoader) {
        $LoaderPath = $systemLoader
    }
}
if (-not $LoaderPath -or -not (Test-Path $LoaderPath)) {
    throw "OpenXR loader not found. Pass -LoaderPath explicitly."
}

$build = Join-Path $repo "build-root/openxr-runtime-probe"
cmake -S $repo -B $build -A x64 `
    -DLTR_ENABLE_OPENXR_RUNTIME_PROBE=ON `
    "-DLTR_OPENXR_SDK_ROOT=$OpenXRSDKRoot"
if ($LASTEXITCODE -ne 0) {
    throw "cmake configure failed with exit code $LASTEXITCODE"
}

cmake --build $build --config Release --target ltr_openxr_runtime_probe
if ($LASTEXITCODE -ne 0) {
    throw "cmake build failed with exit code $LASTEXITCODE"
}

$probe = Join-Path $build "Release/ltr_openxr_runtime_probe.exe"
& $probe --loader $LoaderPath
if ($LASTEXITCODE -ne 0) {
    throw "OpenXR runtime probe failed with exit code $LASTEXITCODE"
}
