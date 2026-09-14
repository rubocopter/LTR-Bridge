param(
    [Parameter(Mandatory = $true)]
    [string]$XessSdkRoot,

    [string[]]$Resolutions = @("256x144", "1280x720", "1920x1080", "2560x1440", "3840x2160"),

    [ValidateRange(1, 100)]
    [int]$Runs = 5,

    [ValidateRange(3, 240)]
    [int]$Frames = 12,

    [string]$Configuration = "Release",

    [string]$OutputCsv = ""
)

$ErrorActionPreference = "Stop"
$repoRoot = Split-Path -Parent $PSScriptRoot
$sdkRoot = (Resolve-Path $XessSdkRoot).Path
$results = [System.Collections.Generic.List[object]]::new()

function Invoke-Checked {
    param(
        [Parameter(Mandatory = $true)][string]$Program,
        [Parameter(Mandatory = $true)][string[]]$Arguments
    )

    & $Program @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "$Program failed with exit code $LASTEXITCODE"
    }
}

foreach ($resolution in $Resolutions) {
    if ($resolution -notmatch '^(?<width>[1-9][0-9]*)x(?<height>[1-9][0-9]*)$') {
        throw "Invalid resolution '$resolution'. Expected WIDTHxHEIGHT, for example 1920x1080."
    }

    $width = [int]$Matches.width
    $height = [int]$Matches.height
    $buildDir = Join-Path $repoRoot "build-root/xess-measure-$($width)x$($height)"

    Invoke-Checked cmake @(
        "-S", $repoRoot,
        "-B", $buildDir,
        "-A", "x64",
        "-DLTR_ENABLE_XESS_NATIVE_AA_PROBE=ON",
        "-DLTR_XESS_SDK_ROOT=$sdkRoot",
        "-DLTR_XESS_PROBE_WIDTH=$width",
        "-DLTR_XESS_PROBE_HEIGHT=$height",
        "-DLTR_XESS_PROBE_FRAMES=$Frames"
    )
    Invoke-Checked cmake @(
        "--build", $buildDir,
        "--config", $Configuration,
        "--target", "ltr_xess_native_aa_probe"
    )

    $exe = Join-Path $buildDir "$Configuration/ltr_xess_native_aa_probe.exe"
    if (-not (Test-Path $exe)) {
        throw "Probe executable not found: $exe"
    }

    for ($run = 1; $run -le $Runs; ++$run) {
        $output = (& $exe 2>&1) -join "`n"
        if ($LASTEXITCODE -ne 0 -or $output -notmatch 'RESULT PASS') {
            throw "Probe failed for $resolution run $run`n$output"
        }

        if ($output -notmatch 'gpu_ms baseline_mean=(?<baselineMean>[0-9.]+) baseline_min=(?<baselineMin>[0-9.]+) baseline_max=(?<baselineMax>[0-9.]+) responsive_mean=(?<responsiveMean>[0-9.]+) responsive_min=(?<responsiveMin>[0-9.]+) responsive_max=(?<responsiveMax>[0-9.]+)') {
            throw "GPU timing line missing for $resolution run $run"
        }
        $timing = @{
            baselineMean = [double]$Matches.baselineMean
            baselineMin = [double]$Matches.baselineMin
            baselineMax = [double]$Matches.baselineMax
            responsiveMean = [double]$Matches.responsiveMean
            responsiveMin = [double]$Matches.responsiveMin
            responsiveMax = [double]$Matches.responsiveMax
        }

        if ($output -notmatch 'temporary_heap_bytes baseline_buffer=(?<baselineBuffer>[0-9]+) baseline_texture=(?<baselineTexture>[0-9]+) responsive_buffer=(?<responsiveBuffer>[0-9]+) responsive_texture=(?<responsiveTexture>[0-9]+)') {
            throw "Temporary heap line missing for $resolution run $run"
        }

        $results.Add([pscustomobject]@{
            Resolution = $resolution
            Width = $width
            Height = $height
            Run = $run
            BaselineMeanMs = $timing.baselineMean
            BaselineMinMs = $timing.baselineMin
            BaselineMaxMs = $timing.baselineMax
            ResponsiveMeanMs = $timing.responsiveMean
            ResponsiveMinMs = $timing.responsiveMin
            ResponsiveMaxMs = $timing.responsiveMax
            BaselineTempBytes = [uint64]$Matches.baselineBuffer + [uint64]$Matches.baselineTexture
            ResponsiveTempBytes = [uint64]$Matches.responsiveBuffer + [uint64]$Matches.responsiveTexture
        })
    }
}

$summary = foreach ($resolution in $Resolutions) {
    $samples = @($results | Where-Object Resolution -eq $resolution)
    $baselineMeans = $samples.BaselineMeanMs | Measure-Object -Average -Minimum -Maximum
    $responsiveMeans = $samples.ResponsiveMeanMs | Measure-Object -Average -Minimum -Maximum

    [pscustomobject]@{
        Resolution = $resolution
        Runs = $samples.Count
        BaselineMeanMs = [math]::Round($baselineMeans.Average, 4)
        BaselineRunMinMs = [math]::Round($baselineMeans.Minimum, 4)
        BaselineRunMaxMs = [math]::Round($baselineMeans.Maximum, 4)
        ResponsiveMeanMs = [math]::Round($responsiveMeans.Average, 4)
        ResponsiveRunMinMs = [math]::Round($responsiveMeans.Minimum, 4)
        ResponsiveRunMaxMs = [math]::Round($responsiveMeans.Maximum, 4)
        TempMiBPerContext = [math]::Round(($samples[0].BaselineTempBytes / 1MB), 4)
    }
}

$summary | Format-Table -AutoSize

if (-not $OutputCsv) {
    $OutputCsv = Join-Path $repoRoot "build-root/xess-measurements.csv"
}
$summary | Export-Csv -NoTypeInformation -Encoding UTF8 $OutputCsv
Write-Host "Wrote summary: $OutputCsv"
