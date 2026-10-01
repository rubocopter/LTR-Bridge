$ErrorActionPreference = 'Stop'

. (Join-Path $PSScriptRoot '..\tools\d3d9_real_observer_validation.ps1')

$postReset = @'
event=real_multiframe_client stage=initialized protocol=1 generation=1 ring_depth=2 frames=12 width=2560 height=1440 adapter_luid=84543
event=real_multiframe_bridge stage=initialized device_id=1 generation=1 width=2560 height=1440 format=21 ring_depth=2 frames=12 initial_stall_ms=15000 validation_readback=0 source_reuse=done_fence
event=real_multiframe_bridge stage=shutdown reason=reset_begin device_id=1 generation=1 submitted=2 done_completed=0 fail_open=1
event=real_multiframe_client generation=1 frames_submitted=2 backpressure_checks=58 child_liveness_checks=62 ready_completed=18446744073709551615 done_completed=18446744073709551615 child_exit=90 RESULT CANCELLED
event=reset device_id=1 resource_generation=2 width=1920 height=1080
event=first_state_sample boundary=end_scene device_id=1 resource_generation=2 rt_observed=0 depth_observed=0 transforms_observed=0 swapchains=0
event=state_sample boundary=present device_id=1 resource_generation=2 rt_observed=1 depth_observed=1 transforms_observed=1 swapchains=1
event=real_multiframe_bridge stage=completed device_id=1 generation=2 backpressure_skips=3 capture_failures=0 RESULT PASS
event=real_multiframe_client generation=2 frames_submitted=12 backpressure_checks=3 child_liveness_checks=4 ready_completed=12 done_completed=12 child_exit=0 RESULT PASS
event=x64_real_bridge_multiframe stage=completion final_ready=12 final_done=12 generation=2 frames=12 consumer_copies=12 validation_mismatches=0 RESULT PASS
'@

if ((Get-LtrResetGeneration -Text $postReset) -ne 2) { exit 1 }
if (-not (Test-LtrResetGenerationEvidence -Text $postReset -Generation 2)) { exit 2 }
if (-not (Test-LtrManagedSemanticCompletion -Text $postReset -RequireResetGeneration -ResetGeneration 2)) { exit 3 }
if (Test-LtrManagedSemanticCompletion -Text $postReset -ResetGeneration 2) { exit 4 }

$normal = $postReset + "`nevent=summary device_id=1 end_scene=120 present=120 resets=0 resource_generation=1 OBSERVATION_RESULT OBSERVED"
if (-not (Test-LtrManagedSemanticCompletion -Text $normal -ResetGeneration 2)) { exit 5 }

$incomplete = $postReset -replace 'event=state_sample boundary=present device_id=1 resource_generation=2 rt_observed=1 depth_observed=1 transforms_observed=1 swapchains=1', 'event=state_sample boundary=present device_id=1 resource_generation=2 rt_observed=1 depth_observed=0 transforms_observed=1 swapchains=1'
if (Test-LtrResetGenerationEvidence -Text $incomplete -Generation 2) { exit 6 }

$inactive = $postReset -replace '(?m)^event=real_multiframe_(client|bridge) stage=initialized[^\r\n]*\r?\n', ''
$inactive = $inactive -replace '(?m)^event=real_multiframe_client generation=1[^\r\n]*\r?\n', ''
$inactive = $inactive -replace 'generation=1 submitted=2', 'generation=1 submitted=0'
$inactive = "event=real_multiframe_bridge stage=transport_start device_id=1 generation=1 hr=0x80004005 fail_open=1 RESULT FAIL`n" + $inactive
if (Test-LtrResetGenerationEvidence -Text $inactive -Generation 2) {
    throw 'An inactive generation-1 startup failure must not establish active reset cancellation'
}

$uncancelled = $postReset -replace '(?m)^event=real_multiframe_client generation=1[^\r\n]*\r?\n', ''
if (Test-LtrResetGenerationEvidence -Text $uncancelled -Generation 2) {
    throw 'Reset teardown without client cancellation evidence must be rejected'
}

$wrongCancellation = $postReset -replace 'generation=1 frames_submitted=2', 'generation=3 frames_submitted=2'
if (Test-LtrResetGenerationEvidence -Text $wrongCancellation -Generation 2) {
    throw 'Cancellation from another generation must not satisfy the reset gate'
}

$wrongDevice = $postReset -replace 'stage=initialized device_id=1 generation=1', 'stage=initialized device_id=2 generation=1'
if (Test-LtrResetGenerationEvidence -Text $wrongDevice -Generation 2) {
    throw 'Initialization on another device must not satisfy the reset gate'
}

$failedCancellation = $postReset -replace 'child_exit=90 RESULT CANCELLED', 'child_exit=90 RESULT FAIL'
if (Test-LtrResetGenerationEvidence -Text $failedCancellation -Generation 2) {
    throw 'A failed client completion must not establish expected cancellation'
}

$unsent = $postReset -replace 'generation=1 submitted=2', 'generation=1 submitted=0'
$unsent = $unsent -replace 'generation=1 frames_submitted=2', 'generation=1 frames_submitted=0'
if (-not (Test-LtrResetGenerationEvidence -Text $unsent -Generation 2)) {
    throw 'An initialized unsent bridge can be actively cancelled by reset'
}

Write-Output 'observer_validation_checks=12 RESULT PASS'
exit 0
