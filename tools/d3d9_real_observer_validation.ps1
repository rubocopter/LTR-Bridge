function Get-LtrResetGeneration {
    param([Parameter(Mandatory = $true)][string]$Text)
    $matches = [regex]::Matches($Text, "event=reset .*resource_generation=(\d+)")
    if (-not $matches.Count) { return 0 }
    return [int]$matches[$matches.Count - 1].Groups[1].Value
}

function Test-LtrResetGenerationEvidence {
    param(
        [Parameter(Mandatory = $true)][string]$Text,
        [Parameter(Mandatory = $true)][int]$Generation
    )
    if ($Generation -le 1) { return $false }
    $resets = [regex]::Matches($Text, "(?m)^event=reset device_id=(\d+) resource_generation=$Generation ")
    if (-not $resets.Count) { return $false }
    $reset = $resets[$resets.Count - 1]
    $deviceId = $reset.Groups[1].Value
    $priorGeneration = $Generation - 1
    $beforeReset = $Text.Substring(0, $reset.Index)
    $shutdowns = [regex]::Matches($beforeReset, "(?m)^event=real_multiframe_bridge stage=shutdown reason=reset_begin device_id=$deviceId generation=$priorGeneration submitted=(\d+) ")
    if (-not $shutdowns.Count) { return $false }
    $shutdown = $shutdowns[$shutdowns.Count - 1]
    $submitted = $shutdown.Groups[1].Value
    $beforeShutdown = $beforeReset.Substring(0, $shutdown.Index)
    $afterShutdown = $beforeReset.Substring($shutdown.Index + $shutdown.Length)

    # Observer initialization is emitted only after Client::Start succeeds.
    # An active bridge may be cancelled before its first successful submission.
    if ($beforeShutdown -notmatch "(?m)^event=real_multiframe_bridge stage=initialized device_id=$deviceId generation=$priorGeneration " -or
        $afterShutdown -notmatch "(?m)^event=real_multiframe_client generation=$priorGeneration frames_submitted=$submitted [^\r\n]*child_exit=90 RESULT CANCELLED(?:\r?$)") {
        return $false
    }

    $afterReset = $Text.Substring($reset.Index)
    return $afterReset -match "event=state_sample .*device_id=$deviceId resource_generation=$Generation .*rt_observed=1 .*depth_observed=1 .*transforms_observed=1" -and
        $afterReset -match "event=real_multiframe_bridge stage=completed device_id=$deviceId generation=$Generation .*RESULT PASS" -and
        $afterReset -match "event=real_multiframe_client generation=$Generation frames_submitted=12 .*RESULT PASS" -and
        $afterReset -match "event=x64_real_bridge_multiframe stage=completion .*generation=$Generation frames=12 consumer_copies=12 .*RESULT PASS"
}

function Test-LtrManagedSemanticCompletion {
    param(
        [Parameter(Mandatory = $true)][string]$Text,
        [switch]$RequireResetGeneration,
        [int]$ResetGeneration = 0
    )
    if ($RequireResetGeneration) {
        return Test-LtrResetGenerationEvidence -Text $Text -Generation $ResetGeneration
    }
    return $Text -match "event=summary .*present=[1-9][0-9]{2,} .*OBSERVATION_RESULT OBSERVED"
}
