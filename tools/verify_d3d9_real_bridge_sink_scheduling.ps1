$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent $PSScriptRoot
$sinkSource = Join-Path $repoRoot "src/d3d9_real_bridge_sink/main.cpp"
$text = Get-Content $sinkSource -Raw

if ($text -match 'queue->Wait\(ready_fence\.Get\(\), ready\)') {
    throw "The real bridge sink still enqueues GPU waits for producer-ready values that may not exist yet"
}

if ($text -notmatch 'const HANDLE waits\[\]\s*=\s*\{event\.get\(\),\s*parent\};' -or
    $text -notmatch 'WaitForMultipleObjects\(2,\s*waits,\s*FALSE,\s*INFINITE\)') {
    throw "The real bridge sink does not CPU-gate producer-ready waits on both the fence event and parent-process liveness"
}

if ($text -notmatch 'wait_for_producer_ready\(ready_fence\.Get\(\),\s*ready,\s*parent\.get\(\)\)') {
    throw "The multiframe loop is not using the parent-cancellable producer-ready wait"
}

if ($text -match 'WaitForSingleObject\([^,]+,\s*10000\)[^\r\n]*producer') {
    throw "The real bridge sink still treats a temporary producer frame gap as a fixed 10-second transport failure"
}

if ($text -notmatch 'OpenProcess\(PROCESS_DUP_HANDLE\s*\|\s*SYNCHRONIZE') {
    throw "The real bridge sink parent handle is not opened with synchronization rights"
}

Write-Output "real_bridge_sink_scheduling=PASS"
