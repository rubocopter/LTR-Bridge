# Codex handoff

Updated: 2026-10-01.

## Latest checkpoint — native menu mouse events and continuous body yaw

**Verified / observed:** CoJ run `20261001T133145Z-5a8de35f18bf` from source
`b0aa3112a7f9799b6fa086f2487c69b69a39e24b` finished and restored staging.
The operator rejected automatic pointer operation: no option hover/selection,
retained bullet target after a button press, and mouse needed for menus. Clip:
`C:/Users/onita/Videos/clip_1.790.862.253.195.mp4`. Fresh retained-package audit
passes 98 identity/hash/size checks. Manifest
`6D936892119EBDC2DDFCE781A17F69C0A2DF95BE05EC2819B683AC3F3EC43EE0`,
package SHA-256 `B17E4F02BD4D214E6CFA144BCD01EEBE197F53AEC6966D95EFDFA31B42E30C24`.
Package/extract/verifier/clip frames and exact-binary probes are retained in CoJ
ignored `work/transport-diagnosis/20261001T133145Z-5a8de35f18bf/`.
Earlier active-candidate descriptions below are historical and superseded.

**Visual-validated / observed:** operator again confirms depth, head turns,
controller turn, movement/walk/jump and recenter. Native revolver hand/body steps
on physical yaw despite Body IK disabled. Configured HMD refresh/producer target
remain 90/90 Hz; normal same-owner shutdown and zero pending consumer copies are
recorded. These observations do not accept tracked Body IK or weapon alignment.

**Verified:** sprite `UICursor.GetPos` echo does not establish native hover.
`OnMouseMove` moves the sprite, `SetProcessMouse` only enables a flag. Actual UI
`GetMousePos` reads X/Z from `Sprite+0x180` input context, or owning module+0xF4,
with native X/Y at context+0x38/+0x3C. Shipped recursive dispatcher RVA `0xC8F00`
emits enter/move/leave through native controls after bounds/capture tests. A
local x86 probe maps the exact DLL without initialization and executes this
actual code on synthetic sprites: coordinate-only writes cause zero events;
recursive dispatch produces child enter/move/leave and honors disabled roots.
Exact contracts/hash scope live in CoJ `docs/research/COJ_UI_MOUSE_PATH.md`.

**Implemented / host-tested:** separately authorized CoJ fix dispatches native
mouse events before visual cursor SetPos and gates delivery against actual UI
input readback. Pointer lookup uses non-creating FindUI(index); passive cursor
observation reads existing m_cCursor instead of invoking factories. Failed
initial observation, delivery or readback cancels its pending click and frees
the retained target. Review found the lazy-loading lookup and early-failure
stale click; both were reproduced as failing regressions and corrected. The
automatic ray, same-hand trigger and temporary mouse priority remain.

**Verified / implemented / host-tested:** 35-degree engage / 20-degree residual
body-yaw policy produces at least 15-degree actor jumps even with Body IK off;
frozen log contains corresponding 15–16-degree deltas. Continuous following
retains the 35-degree free-look cone and absorbs only excess yaw. Regression
reproduced 15.5 degrees of actor movement for 0.5 degrees of head movement;
corrected policy bounds gradual actor increments by the actual head increment,
including reversal/wrapping/recenter and previous-owned-yaw compensation.

**Experiment-pending:** replacement remains physically unaccepted. Next run
combines main/pause hover and short L2/R2 selection, Cross/Circle, physical mouse
coexistence, slow physical yaw while watching the native revolver hand with IK
off, depth/head turns, move/walk/jump, Create recenter, dashboard return and normal
quit/finish. Transport PASS cannot accept menu/body visual gates. No game or
SteamVR auto launch. LTR source remains independent; there is no new real-game
temporal input, reconstruction return path or LTR headset validation.
Publication, final fresh checks and immutable staging identity will be recorded
below after this replacement is finalized. Existing LTR probe.obj and
d3d9ex-managed-pool-probe.obj remain untouched.

## Historical checkpoint — held pointer rejected; automatic internal-cursor correction

**Verified / observed:** CoJ run `20261001T004616Z-6768e557f531` completed
startup/normal closure and restored staging, but the operator rejected menu
usability: holding R1 activated the ray while the game cursor still jumped.
The clip `C:/Users/onita/Videos/clip_1.790.857.394.234.mp4` shows the yellow
cursor moving independently of the relatively stable cyan ray endpoint.
Fresh package verification passes 98 identity/hash/size checks for source
`b67c051287e4259aa074ceaa57b1f34eb5df6b47`, manifest
`1B0E8C418BD14C676BFCF61039F7DDFA42B52FD1300C5C9DEB83B4AC22A8516F`,
ZIP SHA-256 `35F11CAB3CB3D695D2DC2FCB4E3D605742EA7548D17977F179E2E0E15AC75EC6`.
The retained package, extract, verifier and clip contact sheet are in CoJ ignored
`work/transport-diagnosis/20261001T004616Z-6768e557f531/`.
Read-only status before preparing a replacement: game not running, staging none.
Earlier active-candidate descriptions below are historical and superseded.

**Verified / hypothesis:** static inspection of the exact shipped Java cursor
and ChromeEngine3 shows `UICursor.SetPos` calls an internal logical cursor setter
and sprite position; JNI `GameObject.SetCursorPos` RVA `0xB21D0` does not move the
Windows cursor. ChromeEngine3 SHA-256 remains
`DB69BC35919FE57187766771A2452ACA11090474F6D63DF1A85A80EDED131EC8`.
The rejected route performs Windows warp, absolute SendInput and WM_MOUSEMOVE
for every ray update. Relative mouse-stream feedback explains the discrepancy,
but remains a hypothesis until the isolated replacement is physically exercised.

**Implemented / host-tested:** separately authorized CoJ correction removes
all desktop movement injection, queues source-pixel targets on the presenter
and delivers only on the game thread via `MainMenuModule.GetGlobalCursor` ->
`UICursor.SetPos` + `OnMouseMove`. `UICursor.GetPos` observes the logical cursor
before/after delivery; matching readback gates selection. A trigger retains its
target until the next game Present has observed it applied, allowing hover
processing before selection. Accepted short taps survive ordinary trigger release.
Back/Cross navigation, focus/owner loss and a change in shipped static
`MainMenuModule.m_nCurUI` cancel the prior queue/hover. The static int field
is confirmed in shipped `code.pak`, SHA-256
`F9DB47C166E03F23E37CBCDFD5344E4AD4C5C9134F35E8F6DCDF66DB7E71CE12`.
Same-index modal-dialog lifecycle is not independently validated. Cursor/index validation
and native selection are serialized with mailbox cancellation; hand/claim/click
identity protects completion.
The laser is automatic with no R1/L1 requirement, as the operator requested;
right tracked hand is preferred, a fresh other-hand trigger chooses that ray,
and same-hand L2/R2 selects. Physical logical-cursor movement/held mouse button
has priority for 1.5 seconds after the latest activity; observation continues
while the ray is hidden. Cross/Circle and gameplay weapon-transition guards remain.

**Host-tested:** fresh CoJ Release build and CTest pass 36 tests, 1 classic
sharing skip, 0 failures. New regressions execute the actual production delivery
callback without desktop/game input and the production JNI adapter with fake JNI
function tables. RED/GREEN checks reproduce 200 desktop injections for 200 fixed
ray updates, missing mouse priority and premature click delivery, then verify
removal/priority/hover behavior. JNI fixtures cover global/active menu lookup,
null current UI, reference cleanup, exceptions, missing APIs and nonfinite reads.
The full-UI verifier now requires sole internal-cursor delivery with matching
readback, rejecting simultaneous Windows ownership. Targeted regressions also
cover brief taps, menu-index changes at unchanged coordinates, stale dispatch
after navigation, and missing JNI static-int observation. Review findings on
lost taps and clicks crossing menus were reproduced and addressed. These checks do not prove physical
hover, correct menu-option selection or mouse coexistence.

**Observed reference:** read-only Penumbra checkout
`E:/penumbra_vr`, commit `6100ede24e95716ef2e170e6887a6e550651ca2b`, gives
physical mouse motion temporary priority at the native cursor consumer. CoJ
implements its own exact-game route; no GPL source is copied and no dependency
is introduced. LTR source remains independent, with no new reconstruction,
temporal-input, transport-reset or headset validation from this CoJ correction.

**Experiment-pending:** the operator requested a broader next run. Prepare
the normal transport profile with stereo/tracking enabled and Body IK disabled.
In one manual run check automatic menu hover, R2/L2 selection, Cross/Circle and
mouse/drag priority; load a save, judge stereo depth/head turns at configured
HMD cadence, move/walk/jump and recenter; open/close the dashboard, revisit the
pause UI and quit normally, then `finish`. The transport verifier establishes
transport/shutdown only; menu, recenter and resumed usability need operator
confirmation even if its verifier passes. No game/SteamVR is launched automatically. CoJ code/tests/durable docs are published to `origin/main` as
`b0aa3112a7f9799b6fa086f2487c69b69a39e24b`; the working tree is clean.
The new candidate's immutable source/build/run identity will be recorded here
after preparation. CoJ README/architecture/validation/roadmap now record the real
rejection and distinguish implemented/host-tested correction from physical acceptance.

## Historical candidate — sprite-only automatic pointer, now finished/rejected

**Host-tested / verified staging:** normal CoJ `prepare` from clean published
source `b0aa3112a7f9799b6fa086f2487c69b69a39e24b` rebuilt Release and passed
36 tests, 1 classic-sharing skip, 0 failures. Run `20261001T133145Z-5a8de35f18bf`,
manifest `6D936892119EBDC2DDFCE781A17F69C0A2DF95BE05EC2819B683AC3F3EC43EE0`,
manifest-file SHA-256
`D99CCC8BE3F2F1546BEBC02B6B97DD18D7CB34FC23B6E6D63F6FE97B6C268024`,
proxy `12166C06C4E64024E6A63E1AA418A9FDF8B470338669160809406E97CD5450A1`.
Thirteen fresh candidate checks verify source/run/stage identity, archived manifest,
all four deployed hashes, x86 proxy and intended control state. Read-only status:
game not running, native-stereo transport profile staged, tracking/gameplay input,
capture and both eyes enabled, `vr-full` movement trace, Body IK disabled.
Reversible 1920x1080/FSAA0 profile is active. No game/SteamVR auto launch.

**Experiment-pending:** do not prepare over this candidate. In one manual run
exercise automatic menu hover (no R1), short R2/L2 selection, Cross/Circle,
mouse/drag priority; load a save and check stereo/head turns, move/walk/jump,
recenter with Create, open/close the dashboard, revisit pause-menu pointing,
then quit normally and `pwsh -File E:/call_of_juarez_vr/tools/vr_test.ps1 finish`.
Body/arm/weapon alignment is not accepted by this Body-IK-off profile. The
transport verifier's PASS cannot accept menu accuracy, recenter visual quality
or resumed dashboard usability; retain explicit operator observations.
Local procedure: CoJ ignored `work/transport-diagnosis/UI_POINTER_CANDIDATE.md`.
Fresh preparation log and 13-check staging audit: `prepare-automatic-pointer-combined.log`
and `automatic-pointer-combined-verification.json` in the same local directory.
Rejected held-pointer notes are preserved inside the rejected run's local folder.

## Historical documentation checkpoint — project landings

**Verified / implemented:** both project READMEs now distinguish accepted bounded
results from current gates. LTR's architecture, compatibility matrix, roadmap,
D3D9 conclusion and agent phase reflect the already live-tested reset-generation
join rather than its earlier compile-only state. CoJ's landing and roadmap
reflect accepted native transport, configured-HMD rate cap and normal-quit
shutdown; the new held-shoulder menu pointer remains host-tested with physical
acceptance pending. Body/arm IK and weapon alignment remain unaccepted.

This is a documentation-only pass. No new runtime, reconstruction, headset or
performance result is claimed. Preserve the active menu candidate below: its
immutable source identity remains `b67c051287e4259aa074ceaa57b1f34eb5df6b47`
even after newer documentation commits. Earlier checkpoints below are historical
and must not override the latest result or active candidate.

**Verified / published:** CoJ documentation is pushed to `origin/main` as
`57923db1de3a95e45f92d72beaa585bb39477678`. Local link checks resolve 11 links
in each repository's reviewed documents; CoJ's landing image and menu acceptance
anchor also resolve. Both diffs pass whitespace checks. Read-only CoJ status
still reports the same startup candidate, game not running, and its deployed
proxy SHA-256 matches the value recorded below. No build or physical run was
performed for this documentation-only update.

## Historical checkpoint — HMD rate cap accepted; menu ownership correction

**Verified / live-tested / performance-validated for bounded cadence:** user
completed and finished CoJ run `20261001T000751Z-095a23ec5916`, source
`e479e447e74871cfa37ab790ba2775234bd93a75`, manifest
`CA8D4223E9A0CF434DA69ADFDE251307B652971FBFFB1DB56363BD9BCFE8C7AD`.
Fresh package verification passes 98 identity/hash/size checks and independently
reproduces ZIP SHA-256
`E1EB8043F8C039BC361C02C40C7FD8FD5361BF462FA9567077F508A3DF915C46`.
All four artifact snapshots match their recorded finish-time deployment hashes.
Both summaries replay successfully. The native render window is 35.427 seconds:
HMD property/producer target 90/90 Hz, update 87.649 Hz, pair 86.995 Hz, presenter
new/total 84.906/88.717 Hz. This closes the bounded configured-rate gate; it does
not establish exact 90 fresh frames/s, phase alignment or sustained tail latency.

**Observed:** the operator reports that it feels much better. Two ring drops,
72 mailbox replacements and frame-age p95/max 26.390/90.730 ms remain recorded.
Whole-run copy-completion max 147.233 ms has no phase attribution. Sampled native
CPU readback/copy and producer/consumer GPU waits are zero; intentional CPU
render scheduling remains separately measured. No broader performance/support
claim is implied. Refresh changes and missing-rate/reset recovery remain unproved.

**Live-tested normal closure:** 3,009 copies and completions agree, all 3,081
published producer leases are released, final pending/ring-depth/abandoned counts
are zero, and same-owner OpenVR shutdown plus matching run_end complete. Finish
restored staging and Video.scr. Raw package/extract/replay/audits are retained in
CoJ ignored `work/transport-diagnosis/20261001T000751Z-095a23ec5916/`.

**Observed / implemented / host-tested / experiment-pending:** the operator
reports an unusable menu cursor unless both controllers stay completely still.
Source confirms the prior always-active ray continuously injects Windows cursor
motion and can select from a hand different from the projected ray. The user
chose hold-L1/R1 activation. Separately authorized CoJ correction is committed
and pushed as `b67c051287e4259aa074ceaa57b1f34eb5df6b47`: passive tracking does
not claim the cursor; shoulder-held ownership stays with that hand until release;
same-hand L2/R2 selects; Cross/Circle retain global accept/back. Input/pose/focus
loss requires a fresh active release/press. Accepted motion queues selection by
hand/claim/click, so stale events/completions cannot transfer to later input.
Menu focus gates dispatch without starving flat capture; held menu activation
cannot switch gameplay weapons before release. No concurrent JNI cursor-motion
route is introduced. Fresh CoJ Release build/CTest passes 34 tests, 1 classic
sharing skip, 0 failures; focused review has no remaining blockers. These changes
are host-tested only; menu accuracy/usability needs the isolated manual gesture.
LTR remains independent and gains no new temporal/reconstruction validation.

## Historical candidate — explicit menu pointer (completed; pointer rejected)

**Host-tested / verified staging:** fresh CoJ `prepare -StartupOnly` from clean
published commit `b67c051287e4259aa074ceaa57b1f34eb5df6b47` rebuilt Release
and passed 34 tests, with 1 classic-sharing skip and 0 failures. Run
`20261001T004616Z-6768e557f531`, manifest
`1B0E8C418BD14C676BFCF61039F7DDFA42B52FD1300C5C9DEB83B4AC22A8516F`,
manifest-file SHA-256
`8C04D8BF3758EB48BD01914E8395E384B329643C8B3DCE282E40044A0F490BA6`,
proxy `65B6655A3E8D7DEC61F55A358E0C95246AABD4D24E4A2FBE980195ADF1FCA401`.
Every deployed artifact hash and run/build/clean-source identity matches.
Read-only status: game not running, startup-profile native proxy staged,
camera/native-stereo override and body IK disabled, reversible 1920x1080/FSAA0
profile active. OpenVR flat/menu input remains enabled; no automatic game or
SteamVR launch. Do not prepare over this candidate.

**Experiment-pending:** manually start SteamVR/CoJ and use only the main menu.
Without shoulder buttons, move both controllers while using the physical mouse;
it must remain usable. Hold R1 to aim and R2 to select; repeat L1/L2. Release
the shoulder and immediately verify mouse ownership again. Check Cross accept
and Circle back, avoiding unnecessary setting changes. Quit normally and run
`pwsh -File tools/vr_test.ps1 finish` from CoJ. Startup verification establishes
provenance/presentation/restoration only; operator feedback must establish stable,
accurate selection and coexistence. Native stereo, body/weapon, pending-frame reset
and temporal/backend work are outside this isolated menu gesture. Local procedure
and prepare log: CoJ ignored `work/transport-diagnosis/UI_POINTER_CANDIDATE.md`
and `prepare-pointer-ownership.log`.

## Historical checkpoint — published CoJ baseline and HMD cadence follow-up

**Verified / published:** separate CoJ repository checkpoint
`30b99ad0a49909834f77a7c49bb32a02900ec267` is pushed to `origin/main`.
It contains the bounded D3D9Ex compatibility, asymmetric flat presentation,
consumer-copy drain and exact-build pre-exit OpenVR shutdown corrections,
with the already accepted physical evidence documented in the sections below.
This publication introduces no CoJ dependency into LTR.
The LTR research/lifecycle checkpoint is separately pushed as
`7da5ba8f8883ff7cb2b9266b445c0a397dccfdaa`.

**Observed / user-reported:** the operator identifies the previous approximately
136 Hz producer rate as desktop limited and reports a 90 Hz configured visor.
The accepted native phase separately delivered 6,375 new plus 75 repeat submissions
over 72.280551 seconds: 88.197 new/s and 89.235 total/s. Neither producer updates
nor render pairs are a measurement of configured headset refresh. The old binary
did not log that property; the exact desktop limit remains user-reported.

**Implemented / host-tested / experiment-pending:** the separately authorized
CoJ follow-up, pushed as `e479e447e74871cfa37ab790ba2775234bd93a75`,
reads OpenVR v2.15.6 `Prop_DisplayFrequency_Float` on the presenter
owner, caps one producer frame/pair to that rate and retains `WaitGetPoses` as
the compositor cadence owner. It avoids monitor-vsync waits only when valid HMD
timing is paired with a successful game-owned Create/Reset. Missing later timing
retains the last cap until a safe Reset restores the latest requested interval;
late initial timing waits for that Reset. Flat/native transitions share pacing.
Failed attempts restore the caller's interval before retry; tests cover reuse
of the same parameter object without the game rewriting that field.
This is a rate cap, not phase synchronization. Separate summary fields report
configured HMD refresh, target, producer cadence and compositor delivery. Fresh
CoJ Release CTest passes 33 tests, with 1 classic-sharing skip and 0 failures;
physical HMD cadence remains untested. No game/SteamVR was launched automatically.

**Host-tested LTR checkpoint:** fresh root Release build/CTest passes 7/7.
The managed-compatibility test now executes 28 explicit checks under NDEBUG;
the reset verifier rejects inactive startup failure, unmatched cancellation
and wrong-generation/device evidence, while allowing initialized unsent reset
cancellation. Its 12-check regression and historical accepted reset-log replay
pass. Fresh synthetic 12-frame runs at 64x64 and 2560x1440 with a 50 ms stall
complete with ready/done=12, 12 consumer copies and zero sampled mismatches.
Five fault-injection cases and sink scheduling pass. Eight bootstrap publication
failures return the process handle count to its baseline (542 -> 542).
These are scoped host checks; no new physical CoJ or reconstruction result
is implied.

## Historical candidate — configured HMD rate (completed)

**Host-tested / verified staging (2026-10-01):** fresh CoJ prepare from clean
published commit `e479e447e74871cfa37ab790ba2775234bd93a75` rebuilt Release
and passed 33 tests, with 1 classic-sharing skip and 0 failures. Run
`20261001T000751Z-095a23ec5916`, manifest
`CA8D4223E9A0CF434DA69ADFDE251307B652971FBFFB1DB56363BD9BCFE8C7AD`,
proxy `4C84AA245EF96B161047A0A36CA44F48A4D7E7075F00CEA9F4DE0F3677097F33`.
All four deployed artifact hashes match the manifest; source is clean and
reproducible from the commit. Diagnostic control is `vr-full`, tracking,
gameplay input, capture and both eye renders enabled, body IK disabled.
Read-only status confirms game not running and native-stereo staging active,
with reversible 1920x1080/FSAA0 video settings. No game/SteamVR auto launch.

**Experiment-pending:** operator should manually start SteamVR at the configured
90 Hz, start CoJ, reach gameplay, make slow/fast head turns and move normally for
30–60 seconds, then quit normally and run `pwsh -File tools/vr_test.ps1 finish`
from the CoJ repository. The physical summary must report HMD refresh/producer
target 90 Hz separately from update, pair and new/total presenter rates; rendering
should follow that target rather than the earlier approximately 136 Hz producer
baseline. Verify stable depth/head motion, no transport fallback and complete
shutdown. CPU render-scheduling wait is intentional and separately measured;
producer/consumer GPU waits remain a different metric. This rate-cap experiment
does not validate compositor phase alignment, reset with pending frames, device
loss, temporal inputs, reconstruction or body/UI/weapon work. Do not prepare
another candidate over this staging. Raw procedure/provenance are retained in
CoJ ignored `work/transport-diagnosis/HMD_PACING_CANDIDATE.md` and
`prepare-hmd-pacing.log`.

## Historical checkpoint — bounded CoJ transport cadence accepted

**Verified / live-tested (2026-10-01):** user completed cadence candidate
`20260930T232438Z-8fa08df634c1` and finish. Package SHA-256
`1D6DFFCBB9611E6FA227220018B4D9F66FEDC6E9C4E2D67A9697143736D2F7BF`
matches fresh inspection; every evidence-file hash/size, build/run manifest link
and deployed identity agrees. Manifest
`90C7D0896A7C07B51FE0E6772AC236A25E289D2FFD12D031E0CEDB19EBF001E3`
binds proxy `35096BE6DB5200ECC247B6B276B647B087AD953AA33ABBC2D38F36A8CB68B80E`.
Raw archive/selected extracts and exact-phase summarizer replay are retained in
CoJ ignored `work/transport-diagnosis/20260930T232438Z-8fa08df634c1/`.

**Performance-validated for bounded production cadence:** archived `vr-full`
phase reports 9,811 updates / 72.059864 native simulation seconds = 136.151 Hz,
and 9,796 paired eye renders / 72.280551 render-wall seconds = 135.527 pairs/s.
Independent raw update arithmetic and replay reproduce the summary. This is
63.1%/62.3% above the old CPU-transport reference and 1.42%/1.58% below the
readback-off reference. These are production rates, not headset refresh or
compositor delivery: the phase contains 6,375 new and 75 repeat submissions.
The earlier phase-start-to-last-sample wall interval includes startup and differs
from the render window; do not substitute that denominator into prior comparisons.
Sampled native CPU readback/copy and producer/consumer waits remain zero. Native
frame-age samples have p50 12.561 ms, p95 25.921 ms and max 178.466 ms; 17 ring
drops and mailbox replacement remain observed. Sustained pacing/tail latency is
not promoted by this bounded result.

**Vr-headset-validated / user-confirmed:** user explicitly answers that stereo
depth is correct and the image stable during head turns without jerks/dragging.
This closes the bounded visual transport check with body IK disabled. It does
not promote body/UI/weapon acceptance.

**Live-tested normal closure and telemetry fix:** final GPU/proxy records agree
on 6,376 copies/completions, zero pending or abandoned copies, and zero
open/copy/submit failures or CPU fallback. All 9,795 published producer leases
are reclaimed; final capture depth is now zero, validating the statistic fix.
The log ends with actual same-owner OpenVR completion, capture_end, import
restoration, pre-exit end and matching run_end. Read-only status confirms CoJ
not running and staging none after finish. No game/SteamVR was auto launched.

**Verified next-work boundary:** the short CoJ transport gesture is accepted;
do not repeat the completed cadence/locomotion battery. Durable CoJ docs now
allow body/UI/weapon product work to proceed. Sustained pacing, pending-frame
production reset/device loss and abnormal exit remain independent renderer
gates. LTR temporal-input provenance and XeSS Native AA return/composition remain
unproved and independent; this D3D9Ex/D3D11/OpenVR result adds no D3D12 transport
or reconstruction validation. The active-candidate section below is historical:
after that accepted run no candidate remained staged. The current HMD-rate
candidate is described above.

## Historical candidate — cadence trace (completed)

**Host-tested / verified staging (2026-10-01):** user authorized proceeding with
the remaining cadence/per-eye gate. Fresh CoJ `prepare` rebuilt Release and passed
32 tests, with 1 classic-sharing skip and 0 failures. Active run
`20260930T232438Z-8fa08df634c1`, manifest
`90C7D0896A7C07B51FE0E6772AC236A25E289D2FFD12D031E0CEDB19EBF001E3`,
proxy `35096BE6DB5200ECC247B6B276B647B087AD953AA33ABBC2D38F36A8CB68B80E`.
Every deployed artifact matches its build manifest. The existing diagnostic
control was set to `vr-full`; readback/capture, tracking, gameplay input and both
eye renders are enabled, body IK disabled. Read-only status confirms CoJ not
running, active native-stereo staging and reversible 1920x1080/FSAA0 profile.
This binary includes the host-tested ring-depth statistic correction.

**Experiment-pending:** operator should manually start SteamVR/CoJ, load a save,
spend 30 seconds in ordinary gameplay with slow/fast head turns and check each
eye for a current, distinct image, then quit normally and run CoJ `finish`.
Do not prepare another candidate while this one is staged. The movement summary
must contain the `vr-full` phase with update/stereo-pair cadence; prior empty
phases and mixed flat/native timing samples do not establish Hz. No game/SteamVR
was launched automatically. Raw staging details remain in CoJ ignored
`work/transport-diagnosis/CADENCE_CANDIDATE.md` and prepare-cadence.log.
This active staging supersedes the previous checkpoint's 'staging none' state;
its successful shutdown result and evidence package remain unchanged.

## Historical checkpoint — CoJ normal-quit shutdown accepted

**Live-tested / verified (2026-10-01):** the user manually ran and finished the
separately authorized CoJ transport candidate `20260930T230726Z-960ce616f954`.
The retained package SHA-256 is
`BDBB416A924B844C2A75482418817E81BA147615735B05872D82B05011EE8061`.
Fresh inspection verifies every archived evidence-file hash/size, the build/run
manifest linkage and all deployment identities. Build manifest
`732B92163EBA2699C8B0171C9916C4BAC732F784BC691B925FDE6BDB0AA1D654`
binds proxy `F4D47263C8E6BFD14AE394AF99B8EDBF13E1DF3FF890C008D1D6188C2D6405D9`.
The actual log contains pre-exit installed/begin, same-owner OpenVR
`shutdown_complete=true`, capture_end, import restoration, pre-exit end and
matching run_end. Both unique GPU/proxy final records report 3,206 copies and
3,206 completions, zero pending copies, zero abandoned leases, zero open/copy/
submit failures and no CPU fallback. All 4,979 published producer leases are
reclaimed. Two capture-ring drops are recorded; do not claim zero omitted frames.
This closes the previously observed normal-quit failure on this exact build/host.
It does not establish abnormal-exit recovery or device loss.

**Observed / host-tested telemetry follow-up:** 4,980 producer frames were fenced
and 4,979 collected; the final unpublished frame was cancelled at resource
release. `ReleaseResources` reset slots after its last depth refresh, leaving
the old binary's final `capture_ring_depth=1` stale. A real GPU regression
reproduces this with a fenced unpublished final frame after consumer-copy drain.
Refreshing depth after slot reset passes with final depth zero, one invalidation,
both consumer leases reclaimed and unchanged peak depth. Fresh full Release
build and CTest pass 32 PASS / 1 classic-sharing SKIP / 0 FAIL. This statistic
correction is **implemented / host-tested**, not part of the live-tested binary.
The frozen package is preserved unchanged under CoJ's ignored
`work/transport-diagnosis/20260930T230726Z-960ce616f954/`.

**Verified current state / experiment-pending:** CoJ reports not running and
staging none after finish; no game/SteamVR was launched automatically. The actual
movement summary has an empty phase list because tracing was disabled, so the
sampled/mixed flat-native timings cannot establish update or stereo-pair Hz.
CoJ's next physical gate is bounded `vr-full` cadence tracing and per-eye visual
pairing, with body IK disabled; normal quit remains a regression requirement.
Body/UI/weapon acceptance, focus handoff, pending-frame production reset and
device-loss recovery remain separate. CoJ durable architecture/validation/roadmap
and shutdown/camera/resource research docs now reflect the accepted boundary.
LTR remains independent: this same-process D3D9Ex -> D3D11 -> OpenVR result does
not promote its cross-process D3D12 transport, temporal inputs or reconstruction
backend. LTR's next vertical work remains real temporal-input provenance and
XeSS Native AA return/composition. Earlier pending-candidate statements below
are historical and superseded by this checkpoint; no candidate is currently staged.

## Repository state

The repository is still in research/probe phase. There is no production injector and no existing game-mod repository is a dependency.

**Observed / user-reported current baseline (2026-09-30):** Call of Juarez has been reinstalled clean and the previous VR-mod deployment has been removed from the game installation. Treat the real-game results below as historical evidence from earlier probe runs, not as evidence that any observer, forwarder, VR mod or bridge component is currently present in the game directory. Future LTR_bridge runs should stage only the temporary instrumentation required by the selected probe and verify its removal/restoration afterward.

**Observed current blocker (2026-09-30):** the clean game is user-reported stable when launched normally in its native DX9 and DX10 modes. A fresh observer-only DX9 run also reached the first real `Present` without updating the game's `callstack.txt`. Promoting `Direct3DCreate9` to a `Direct3DCreate9Ex` root still reproduces the game's `CrashExit` path, but the promoted path successfully creates an Ex-capable device and reaches the first real `Present`. The first concrete API incompatibility is later: Chrome Engine requests `CreateTexture(16, 16, 0, 0, format=21/0x15 = D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, ...)`; the Ex-backed device returns `D3DERR_INVALIDCALL (0x8876086C)` with a null texture, after which Chrome dereferences that null texture at `ChromeEngine3.dll+0x23E3FD`. The historical `format=15` wording was a logging-base mistake: `0x15` is decimal 21. This isolates a real D3D9Ex compatibility mismatch before the relay, D3D11/D3D12 transport or multiframe path.

**Live-tested compatibility experiment (2026-09-30):** the opt-in `ManagedTextureFallback` probe initially proved two exact managed-resource failures on this game build. The 16x16 2D texture (`levels=0`, `usage=0`, A8R8G8B8) and the 128-edge cube texture (`levels=1`, `usage=0`, A8R8G8B8) both return `0x8876086C`/null on the Ex-backed device and both return `S_OK`/non-null when retried as `D3DPOOL_DEFAULT`. Those retries move execution past the corresponding null faults, but they do not emulate managed-resource semantics and are diagnostic only.

**Live-tested control-flow result (2026-09-30):** a one-shot software-breakpoint probe pinned to `ChromeEngine3.dll` SHA-256 `DB69BC35919FE57187766771A2452ACA11090474F6D63DF1A85A80EDED131EC8` traced the null returned by Chrome's internal constructor at `+0x230500`. The primary and optional vertex-declaration paths both return non-null objects; seven intercepted `CreateVertexDeclaration` calls return `S_OK`. The direct vertex-buffer failure branch at `+0x2309EB` is not taken. Static disassembly of allocator/cache routine `+0x22FDF0` shows that its cache-miss path calls the device vtable slot `0x68` (`IDirect3DDevice9::CreateVertexBuffer`) with `Pool=1`, i.e. `D3DPOOL_MANAGED`. A new one-shot breakpoint immediately after that call at `+0x22FEC6` live-tested `EAX=0x8876086C` (`D3DERR_INVALIDCALL`). The allocator therefore leaves the out-pointer later consumed at `+0x230AD5` null; `+0x230500` returns null and the caller faults at `+0x22F262`. The third deterministic startup failure is thus another direct D3D9Ex/MANAGED incompatibility, this time for a vertex buffer. The existing texture/cube DEFAULT retries remain diagnostic and do not establish managed-resource emulation.

**Live-tested vtable-refresh and buffer compatibility result (2026-09-30):** Chrome/D3D9 state-block setup refreshes the device vtable during `BeginStateBlock`. The four observed Begin/End pairs are `+0x2345D3/+0x2345EB`, `+0x2348E6/+0x234900`, `+0x234973/+0x234990` and `+0x234B83/+0x234B9B`; a hardware watchpoint traced the rewrite to `d3d9.dll+0x124128` (`rep movsd`). The observed canonical table is `d3d9.dll+0x1008`, with device slot 26 at `+0x1070`. A canonical-slot experiment intercepted the call, but reusable INT3/trap-flag rearming disturbed control flow and is not the selected mechanism. The preferred diagnostic mechanism hooks `IDirect3DDevice9::BeginStateBlock` (vtable index 60) and restores the full relevant observer/compatibility hook set after each refresh. A later live run logged 22 successful `begin_state_block_rehook` events with `observer_hooks=1`. In that run the exact Chrome vertex buffer (`length=0x40000`, `usage=0x8`, `FVF=0`, MANAGED, no shared handle) translated to DEFAULT and was exercised through at least 16 successful Lock/Unlock operations. The next exact managed index buffer (`length=0x20000`, `usage=0x8`, `D3DFMT_INDEX16`, no shared handle) also returned `0x8876086C` from the original call and `S_OK`/non-null from the narrow DEFAULT retry.

**Live-tested DXT1 retry / observed next blocker (2026-09-30):** the fresh Ex run now proves that the exact `CreateTexture(64,64,0,0,D3DFMT_DXT1,D3DPOOL_MANAGED,...)` retry returns `S_OK`/non-null after original `0x8876086C`. The full hook set survives 37 BeginStateBlock refreshes and the tracked vertex buffer reaches 24 successful Lock/Unlock pairs. The next request is `CreateTexture(1,1,1,0,D3DFMT_A8R8G8B8,MANAGED,...)`; it returns `0x8876086C`/null and Chrome faults again at `+0x23E3FD`. The sustained-Present acceptance gate fails. All five existing exact retries are now live-tested as allocations, but startup, contents, texture updates and reset persistence remain unproved. The earlier automatic tool-review rejection is historical; this fresh run actually executed and cleaned up.

**Implemented / live-tested classic startup census (2026-09-30):** `-ResourceCensus` keeps the native classic-D3D9 route, calls each of the five resource-creation methods with unchanged arguments, restores the observer hooks immediately after BeginStateBlock and removes the 64-record cap for these five methods during a bounded observation window. Its acceptance gate was run RED against the previous implementation (missing creation hooks/records), then passed against the new code. The frozen 25-second-window log contains 1,552 creations: 1,548 2D textures (33 DEFAULT, 8 MANAGED, 1,507 SYSTEMMEM), one MANAGED cube, one MANAGED VB, one DEFAULT VB and one MANAGED IB. All creation HRESULTs are `S_OK`; 39 state-block refreshes keep hooks installed and the normal summary records 120 successful Presents, 118 EndScenes and zero resets. Eleven MANAGED calls cover ten distinct profiles, including the new 1x1 texture, 2x2 A4R4G4B4, 256x128 A8R8G8B8, 1024x512 X8R8G8B8 (twice), and 512x256 A8R8G8B8. No volume creation occurs in this window. This is a startup-window census of five methods, not complete gameplay/resource/lifecycle coverage; the summary is an early snapshot, not the final frame count. Raw logs, CSV and summary are under ignored `build-root/evidence/managed-census-2026-09-30/`.

**Implemented / live-tested semantic MANAGED adaptation (2026-09-30):** the bounded compatibility experiment now adapts by resource class instead of allocation size. When the original Ex call fails with `D3DERR_INVALIDCALL`, has no shared handle and otherwise matches the observed safe profile, 2D/cube textures with usage 0 are retried as `D3DPOOL_DEFAULT | D3DUSAGE_DYNAMIC`; MANAGED vertex/index buffers are retried as `D3DPOOL_DEFAULT` while preserving their original usage. The real-game log `C:\Users\onita\AppData\Local\Temp\ltr_d3d9_real_observer_20968.log` records nine adapted creations: six 2D textures, one cube, one vertex buffer and one index buffer. All nine original MANAGED calls fail with `0x8876086C`; all nine retries return `S_OK` with non-null resources. Those objects then service 118 traced CPU-write locks with non-null data: 22 texture/cube surface locks (20 with flags `0x800`, two with `0x2800`), 24 census VB locks plus 64 direct VB lock traces, and eight IB locks. The run survives 39 BeginStateBlock hook refreshes, reaches 118 EndScenes / 120 Presents, records zero exception markers, and ends with `OBSERVATION_RESULT OBSERVED`. No unsupported MANAGED creation is seen in this bounded window. This resolves the previous startup crash path on this build/host and is **live-tested** for the observed startup semantics. It does not establish reset/lost-device equivalence: this run records `resets=0`, and DEFAULT resources do not inherit D3DPOOL_MANAGED reset persistence automatically.

**Live-tested reset-content result (2026-09-30):** a bounded read-only fingerprint probe now covers the real JNI video-apply reset. Nine adapted resources remain alive across `Reset`; generation 1 is followed by generation 2 at `1920x1080`. Four directly lockable adapted 2D textures preserve byte-identical level-0 contents across the reset: resource 1 (`16x16`, A8R8G8B8, 1,024 bytes, hash `0x1f8732193a3f4d25`), resource 6 (`1x1`, A8R8G8B8, 4 bytes, `0x4d25767f9dce13f5`), resource 8 (`256x128`, A8R8G8B8, 131,072 bytes, `0xed273a2b5eee44a5`) and resource 9 (`1024x512`, X8R8G8B8, 2,097,152 bytes, `0xc2c59eb3997b6922`). Resource 9 is then actively bound at texture stage 0 by generation-2 draws. Resources 5 (DXT1) and 7 (A4R4G4B4) reject the direct lock probe with `D3DERR_INVALIDCALL`; this is inconclusive for their contents, not evidence of loss. The run remains stable at 118 EndScenes / 120 Presents with one reset. On this observed reset path, sampled adapted texture contents therefore survive intact; do not add shadow-copy/repopulation machinery without new evidence.

**Live-tested real multiframe/reset lifecycle (2026-09-30):** the two-slot observer/client/x64 sink path has now run inside Call of Juarez through an engine-driven reset. Generation 1 starts at `2560x1440`, submits two frames into a deliberately stalled consumer, and is shut down fail-open at `reset_begin`; the client exits as `RESULT CANCELLED`. After the successful game reset, generation 2 is created at `1920x1080` and completes all 12 submissions. The x64 sink reports `final_ready=12 final_done=12 consumer_copies=12 validation_mismatches=0 RESULT PASS`, the client reports `frames_submitted=12 ... RESULT PASS`, and the observer reports `capture_failures=0 ... RESULT PASS`. This establishes the prepared cancellation/restart generation lifecycle on this build/host. It is lifecycle/content-sample evidence only: no visual, performance, headset or reconstruction-backend validation follows from it.

**Verified / experiment-pending architecture implication (2026-09-30):** the startup-size workaround has been superseded by the bounded semantic adaptation above. A real reset now proves that all nine tracked adapted resources remain alive, the four directly hashable textures keep identical contents, and an old-generation texture is still consumed after reset without destabilizing the observed run. This removes sampled texture-content loss as the working explanation and argues against shadow-copy/repopulation machinery at this stage. Device-loss semantics, non-lockable resources and broader gameplay remain experiment-pending. The separate CoJ mod working tree now contains the directly reusable production changes: MANAGED WRITEONLY VB/IB translation to DEFAULT plus immediate `BeginStateBlock` HookRegistry reacquisition, alongside its existing texture adaptation. Those production changes are host-tested and documented there, but still require a manual exact-game startup/reset run before promotion.

**Host-tested / live-tested census reporting follow-up (2026-09-30):** a traced repeat produced a false acceptance rejection although its frozen log satisfied every predicate. PowerShell regex operators filter arrays rather than return scalar acceptance; splitting the same real log into raw chunks reproduces that false rejection, while joining the chunks restores correct evaluation. Both concurrent observer-log reads now join raw chunks into one string. A fresh traced run passes the runner, records 1,538 successful creations, eleven MANAGED calls, 39 hook refreshes and the 120-Present summary; the exception tracer is installed and records zero access violations. Variable SYSTEMMEM video allocations account for a different total from the first window. Fresh CTest passes 2/2, PowerShell parsing and both repositories' whitespace checks pass, and no temporary probe DLL or CoJ process remains. Frozen traces/manifests are kept in ignored evidence directories in both repositories.

**Observed runner behavior (2026-09-30):** the first flow-probe launch with the runner's default 20-second window produced no observer log because the Steam-launched real `CoJ.exe` appeared after the runner had already cleaned the staged proxy. Repeating the same probe with `-TimeoutSeconds 60` loaded the proxy and produced the evidence above. Treat the 20-second startup window as insufficient for reliable Steam handoff on this host.

**Host-tested unrelated-engine control (2026-09-30):** Steam F.E.A.R. `FEAR.exe` version `1.08.282.0`, SHA-256 `D5EBC38A4F12B772C9112A2811C290ADB6C5052D3BC2F817302D38CF55BB2CBE`, was exercised through a throwaway `d3dx9_27.dll` bootstrap that patches only the executable IAT entry for `Direct3DCreate9`. No `CreateDevice`, device-vtable, relay, D3D11, D3D12, Present, Reset or Release hooks were installed. In baseline mode the hook called the real `Direct3DCreate9`; the game reached its `F.E.A.R.` window and remained alive for the 12-second observation window. Under otherwise identical conditions the hook instead returned a `Direct3DCreate9Ex` object through `IDirect3D9 *`; the log recorded `promote_returned_IDirect3D9Ex_as_IDirect3D9`, and the game again reached its normal window and remained alive for the full 12 seconds. The temporary DLLs, mode flags and log were removed afterward and no F.E.A.R. process remained. This disproves the broad hypothesis that the base-interface promotion mechanism is inherently unstable on this host. The next Call of Juarez work should therefore isolate which Chrome Engine / game assumption differs from F.E.A.R. before reconnecting the multiframe bridge.

The clean Steam Call of Juarez build has now moved the roadmap past observation and one-shot cross-bitness color transport. Real-game observation is host-tested: the game uses the classic D3D9 API, reaches `Present`, and exposes color, depth, transforms and one swapchain there. True classic sharing fails in both tested directions, while the semantic MANAGED adaptation above keeps the promoted D3D9Ex route alive through the bounded 120-Present startup window and preserves every CPU-write pattern observed there. A real reset also preserves the sampled adapted texture contents, including a generation-1 texture that remains actively used in generation 2. The exact two-slot observer/client/x64 topology has now run through the real reset: generation 1 cancels under forced stall and generation 2 completes 12/12 with ready/done=12, 12 D3D12 consumer copies and zero sampled mismatches. Device loss, broader non-lockable-resource semantics, visual correctness, sustained runtime and reconstruction remain pending.

**Historical failed multiframe attempt / superseded design:** the first real-game multiframe attempt initialized a two-slot, 12-frame bridge and launched the persistent x64 sink, but ended at `stage=completion RESULT FAIL`. Offline reproduction resolved the transport-design uncertainty behind that attempt. With D3D11-owned `SHARED_NTHANDLE | SHARED_KEYEDMUTEX` slots, ready/done fence values could reach 12 while x64 D3D12 content validation still read zero for all 12 frames, including a local-texture `CopyResource` source path. That ownership design has been removed. The replacement uses the already-proven general-bridge direction: x64 D3D12 creates two shared BGRA8 slots and `done`, duplicates those handles directly into x86, x86 D3D11 opens them and creates named `ready`, and slot reuse waits on `done` plus child liveness. The client implementation is shared by the synthetic producer and real observer, so the validated protocol code no longer has a second game-only copy. The observer owns two D3D9Ex/D3D11 source relays, one per transport slot. It completes each D3D9 write before the D3D11 transport copy; subsequent reuse of that same relay is allowed only once the client's matching `done` requirement passes. Because D3D12 signals `done` only after waiting for `ready`, and D3D11 signals `ready` after its source copy, the former D3D11 event-query/CPU spin used solely to protect source reuse is redundant and has been removed. It drops capture work under backpressure, terminates the child and releases resources before `Reset`, creates a new generation lazily after successful reset, and disables itself without failing `Present` on transport errors. The completion record reports mean CPU wall time for the D3D9 synchronization region and the now non-blocking D3D11 submit region. This replacement path is now live-tested through the real reset-generation lifecycle described above; the historical failed attempt remains negative evidence only.

Windows CI is configured to compile-smoke the dependency-free optional Win32 probes, including the real-game D3D9 observer forwarder/bootstrap and the new x86 real-bridge transport producer, plus the x64 bridge consumer/sink. External-SDK probes remain conditional and are not silently vendored into CI.

The current source set contains the real-game observer/forwarder, the one-shot-compatible x64 sink, the shared multiframe client/protocol and synthetic producer, the D3D9Ex base-interface and cross-runtime probes, CI compile coverage, and the `TemporalFrame` move into the shared temporal layer. Local `build-*` directories are ignored rather than treated as source. In the no-game validation block, the refactored shared transport client passed 5/5 repetitions at `64x64` and 3/3 at the real `2560x1440` extent, always completing 12/12 frames with forced backpressure and zero content mismatches. After restricting child handle inheritance to the bootstrap pipe, one more `2560x1440` run passed 12/12 with zero mismatches. The source-reuse optimization then replaced the observer's single relay plus D3D11 completion query with two relays governed by the transport slots. The synthetic producer was changed to wait for slot admission before rewriting the corresponding source and to sleep briefly under forced pressure rather than hot-spin; after that change 3/3 `64x64` and 2/2 `2560x1440` runs again passed 12/12 with zero mismatches, while forced-stall backpressure polling fell from tens of thousands of tight-loop checks at `64x64` to roughly 9-11 checks per run. A fresh verification on 2026-09-17 rebuilt both x86/x64 integration targets, reran one 12-frame forced-backpressure pass at `64x64` and one at `2560x1440` with `consumer_copies=12` and zero mismatches, and reran the root x64 CTest suite at 2/2 passing. No game process was launched during these checks.

Current local evidence is concentrated in eight implemented areas:

1. **D3D11 x64 temporal harness — host-tested / visual-validated.** Controlled static, camera, rigid-object, deforming-geometry, masked-particle, blended-transparency, HUD and disocclusion scenarios exercise jitter, shader-readable depth, ground-truth MV, explicit history reset/validity, camera+depth reconstruction and an independent synthetic optical-flow baseline.
2. **XeSS 3.0.2 Native AA probe — host-tested.** D3D12 x64 executes Native AA at 1.0x with the harness Halton jitter convention, current-to-previous render-pixel MV with jitter excluded, explicit reset, responsive-mask coverage and repeated 256x144-to-4K GPU-time / temporary-heap measurements on the current RTX 4070 Ti host. This is not performance validation.
3. **D3D11 x86 -> D3D12 x64 transport — host-tested.** The probe covers 24-frame/two-generation transport, live handle replacement, separate ready/done fences, bounded depth-1/depth-2 backpressure, controlled process/device-loss paths, renderer-to-shared copy/resolve/conversion measurements through 4K, a small format matrix, two-eye isolation and independent synthetic per-eye GPU histories. CPU readback is validation-only.
4. **D3D9Ex relay, controlled scene and external interception boundary — host-tested on the current machine.** The relay route remains stable through 1440p. `d3d9ex_scene_probe` covers engine-owned R10 color, D24S8 depth, vertex resources, transforms and overlapping depth-tested draws, and the scene also reaches the x86 -> x64 bridge. `d3d9ex_intercept_probe` runs the renderer in a separate executable and capture in a separate DLL that hooks `Direct3DCreate9Ex`, `CreateDeviceEx`, `EndScene` and `ResetEx`. Five repeated 12-frame runs preserve the bound color/depth/world state, survive `640x360 -> 1280x720` recreation and finish with zero mismatches; `StretchRect + event` run means average about `0.1990 ms`.
5. **Real Call of Juarez D3D9 observation + historical one-shot x86/x64 compatibility relay — live-tested through bounded D3D9Ex startup/reset semantics.** The clean PE32 Steam game loads system D3D9 and resolves `Direct3DCreate9`. A temporary `d3dx9_29.dll` dependency forwarder/bootstrap installs the observer without modifying the game binaries and is removed after each run. In baseline mode the game creates a true classic-D3D9 device. Earlier opt-in promotion runs backed the same legacy API call with `Direct3DCreate9Ex`; the game's ordinary `CreateDevice` returned an Ex-capable device and reached the real `Present` boundary. A one-shot `2560x1440` A8R8G8B8 relay test succeeded through D3D9Ex `StretchRect`, event-query completion and x86 D3D11 `OpenSharedResource`; the x86 side then copied to an NT-shared BGRA8 resource and a separate x64 D3D12 helper opened/read back it with five matching samples and zero mismatches. D24X8 depth, transforms and one swapchain were visible. Fresh 2026-09-30 tracing established the broader MANAGED pattern rather than five isolated sizes: usage-0 MANAGED 2D/cube textures can be retried as `DEFAULT|DYNAMIC`, while the observed MANAGED WRITEONLY VB/IB profiles can be retried as `DEFAULT` with usage preserved. The bounded semantic adapter live run adapts 9/9 observed allocations, services 118 successful CPU-write lock traces, survives 39 `BeginStateBlock` refreshes and reaches 118 EndScenes / 120 Presents with zero exception markers. A subsequent real reset keeps all nine tracked adapted resources alive; four directly hashable textures preserve identical contents and a generation-1 texture remains actively bound in generation 2. This is scoped evidence for the tested build/host, not generic managed-resource emulation or device-loss equivalence.
6. **dgVoodoo2 2.87.5 comparison and D3D12 addon boundary — host-tested on the current machine.** The native D3D9Ex shared-relay architecture does not traverse dgVoodoo unchanged: R10 target creation is rejected and BGRA8 reaches rendering but shared-relay creation is rejected with `D3DERR_INVALIDCALL`. A separate addon probe built against the external official API package succeeds with the D3D12 backend. Five repeated `d3d12_fl11_0` runs observe API version `0x287`, a non-null D3D12 device, two `640x360 -> 1280x720` swapchain generations and 12/12 matched presentation callbacks with non-null source/destination resources. The callback exposes translated presentation textures; original D3D9 depth/world/MV visibility is not established there.
7. **D3D10.1 -> private D3D11 relay — host-tested on the current machine.** The x86 probe uses a D3D10.1 device at feature level 10.0, copies local R10 color into a legacy shared R10 texture, waits on a D3D10 event query and opens that resource from a same-adapter private D3D11 device. Five standalone `640x360 -> 1280x720` runs pass with zero mismatches. The integrated x86 -> x64 route reuses the existing D3D11 R10-to-RGBA8 conversion and D3D12 host; five 24-frame runs pass with zero mismatches. Bridge-run D3D10 copy+event means were `0.1832–0.1902 ms` and D3D11 conversion means `4.9053–5.2747 us`. Keyed-mutex resource creation returns `E_INVALIDARG` on this RTX 4070 Ti, so event-query synchronization is the proven current-host route. Depth/MV/SM4 temporal integration remains pending.
8. **Real-path two-slot transport contract — host-tested offline and live-tested for the real reset lifecycle.** `src/d3d9_real_bridge_probe/client.*` owns the common x86 client used by both the synthetic producer and observer. The x64 side creates two shared BGRA8 resources and `done`, duplicates their NT handles into x86 and sends a versioned bootstrap record through a pipe; x86 opens them with D3D11, owns the named `ready` fence, performs `CopyResource`, and gates slot reuse on both `done` and child liveness. After the extraction, five `64x64` 12-frame runs with a forced 50 ms consumer stall completed with zero mismatches, and three additional `2560x1440` runs did the same. The observer join uses two reusable D3D9 relays tied to those slots, preserving the D3D9 event-query handoff but using the existing `ready -> done -> reuse` ordering instead of a second D3D11 CPU wait. The synthetic source-reuse variant passed another 3/3 `64x64` and 2/2 `2560x1440` runs with zero mismatches. In the real game, a forced-stall generation 1 was cancelled at reset, then generation 2 at `1920x1080` completed 12/12 with matching ready/done fence values, 12 D3D12 consumer copies, zero sampled mismatches and no capture failures. The earlier D3D11-owned persistent-slot design is retained only as documented negative evidence.

The separate OpenXR x64 bootstrap is **host-tested** for loader/instance/runtime negotiation. SteamVR exposes both D3D11 and D3D12 enable extensions, but the current runs returned `XR_ERROR_FORM_FACTOR_UNAVAILABLE`; no HMD system, graphics session, swapchain, frame loop, presentation or physical-headset claim exists yet.

A 2026-09-30 recheck of the reference projects found BioShock VR DLSS/DLAA, Rogue Trader EnhancedGraphics and upstream OFXR Bridge unchanged at their previously pinned revisions. `Beren5556/W40KRT_VR` has advanced from the first reviewed `v0.9.79-beta` to `v0.9.81-beta` (`0fb98a2f991075256dffd2117f9d458360caa324`). The earlier per-eye NGX, temporal-guide, history-ticket/lease and GPU-ordered interop findings still apply. The newer source advances the neural ABI to v2 with explicit model/preset configuration and tightens the OFXR join: neural-backed synthesis now requires backend `READY` state, no backend failure reason, matching neural generation, and both left/right completed neural frame IDs equal to the game frame. The presentation layer distinguishes descriptor ineligibility from neural incompatibility. **Observed:** this strengthens the case for separate configuration generation, frame/view identity and downstream stage-status validation; it does not change the immediate Call of Juarez gate or justify replacing the host-tested cross-process two-slot transport.

## Evidence boundaries

- All local transport/backend numbers are short, synthetic, single-host measurements unless a document explicitly says otherwise.
- D3D9Ex success, classic-D3D9 shared creation/open failures, the Call of Juarez runtime-promotion compatibility result and BGRA8 behavior are host/driver/game-build scoped.
- dgVoodoo results are scoped to official package/API `2.87.5` on the current host. The proven addon boundary is D3D12 presentation; it does not establish original D3D9 depth, transform or MV visibility.
- D3D10 event-query success and keyed-mutex creation failure are also host/driver scoped; no universal D3D10 synchronization claim follows from this machine.
- Call of Juarez real-game observation, bounded D3D9Ex MANAGED adaptation/reset behavior and the replacement two-slot reset-generation lifecycle are established on this build/host. The dependency-forwarder bootstrap/runtime promotion remains a research mechanism, and no reconstruction backend has yet consumed the real game frame.
- The two-slot x86/x64 mechanics are host-tested offline with content validation and live-tested in the real game for cancellation at reset plus a 12/12 replacement generation. This does not establish sustained runtime, visual correctness, headset behavior or reconstruction output.
- The current stereo transport loop is synthetic and does not establish concurrent full-resolution eye processing, OpenXR pacing or headset latency.
- XeSS Native AA is the only real reconstruction backend executed locally so far; DLSS/DLAA, FidelityFX and XeSS SR comparison remains pending.
- Motion-vector coverage for real skinned/cloth geometry, particles, transparency and first-person/VR objects remains unresolved.
- The controlled D3D9 interceptor is evidence, not reusable runtime code as-is: it relies on simple/global hook state, cooperative loading and controlled lifecycle assumptions. A real adapter must own state per device/swapchain and fail open if the reconstruction path is unavailable.
- D3D9/D3D10 event-query waits, CPU maps/readbacks and synchronous callback logging are validation mechanisms. Their presence in a probe does not justify them in the final per-frame hot path.
- The x86/x64 bridge has substantially more synthetic validation than the current real-renderer path. Further transport optimization should be driven by a real integration measurement or newly observed hazard.
- The project still lacks one shared vertical lifecycle/temporal contract exercised from a real legacy frame through reconstruction; this is the next architectural gate.

## Audit checkpoint — 2026-09-30

**Observed:** the working-tree audit is recorded in [AUDIT_2026-09-30.md](AUDIT_2026-09-30.md), including source references, evidence limits and ordered experiments. No game was launched or instrumented during the audit. Existing code and documentation changes were preserved.

**Host-tested:** a fresh root x64 configure/build passed CTest 2/2. Fresh one-run checks of the shared real-path transport at `64x64` and `2560x1440`, with 12 frames and a forced 50 ms consumer stall, completed 12 consumer copies, ready/done=12 and zero sampled mismatches. The current sink checks only the first pixel per synthetic frame; this is not full-frame validation. Follow-up hardening also passes the two-frame/two-slot edge case (2/2, zero mismatches, zero pressure checks), rejects the removed-device `UINT64_MAX` fence sentinel, exercises invalid transport-resource and done-fence opens, cancels a stalled producer on a declared submission deadline, classifies parent death while waiting for `ready` as `parent_exited`, resumes after an 11,000 ms interframe pause, and deterministically exercises consumer-copy and final-completion failures. The five-case fault-injection verifier passes. Bootstrap failure rollback shows zero measured parent-handle growth across eight fresh repetitions (`616 -> 616`). Focused protocol/completion/handle-ownership CTests pass 3/3 and the scheduling verifier remains PASS.

**Implemented / host-tested:** the targeted synthetic transport review items now have bounded tests: removed-device fence state cannot satisfy progress; duplicated/incoming bootstrap handles have explicit ownership; stalled submission has a deadline and cancellation classification; parent exit is distinguished from a fence failure; temporary producer gaps longer than ten seconds can resume; explicit consumer-copy and final-completion failures terminate without PASS; and two-frame acceptance no longer requires impossible slot-reuse pressure. Real capture numbering must still distinguish omitted game frames from successful transport submissions before temporal inputs are added. Real consumer/capture failure, sustained/reset/device-loss behavior, real multiframe content checks, usable depth/camera/jitter/MV provenance, result return/composition, and output/history identity remain unproved.

**Observed next-work decision:** the bounded semantic adaptation resolves the observed startup allocation failures, a real reset preserves the sampled adapted texture contents, and the real two-slot reset-generation lifecycle completes successfully after cancellation/restart. Do not build managed-resource shadow copies from the earlier assumption. The separate `call_of_juarez_vr` working tree now contains a host-tested production candidate for the directly supported findings: MANAGED WRITEONLY VB/IB translation to DEFAULT and immediate BeginStateBlock hook reacquisition, alongside its existing texture translation. It still needs a manual real-game run before promotion. The planned synthetic lifecycle negatives are now closed; LTR should move to real consumer/capture failure only when it supplies new evidence, otherwise advance to real temporal-input provenance and XeSS Native AA return/composition.

## Next concrete work

**Implemented / host-tested CoJ pre-exit shutdown candidate (2026-10-01):**
the separately authorized production repository now stops its runtime before
the exact executable's imported `ChromeEngine3.dll!DestroyGame`. Static inspection
verifies the executable import RVA `0x9034`, normal post-message-loop call
RVA `0x2105` and engine export RVA `0x2D00`, a no-argument cdecl wrapper.
The adapter gates both known file hashes, x86 headers, loaded call operand and
current import/export identity before touching one protected pointer. It
preserves the original call and foreign hook ownership. Runtime startup requires
successful installation of this boundary.

Finalization stops producers/restores hooks, joins the living presenter, drains
pending GPU copies and reclaims capture resources before forwarding game destroy.
It is idempotent. Runtime state has explicit process lifetime to avoid late
static GPU/thread destructors; atexit performs only a direct incomplete-status
file write without the shared logger mutex. The verifier requires boundary
installation/completion/restoration in addition to real owner and drain evidence.
No success summary is substituted if the boundary is missed. Retained
factory/device roots remain deliberately OS-reclaimed at process exit.

Independent source review found that the previous acceptance regex could still
pass an undrained shutdown with abandoned/pending copies and unequal completion
totals. The verifier now strictly parses unique final GPU/proxy records with
mandatory UInt64 counters, requires zero abandoned/pending copies, copied =
completed in both records and equal copied totals across them. Seven negative
fixtures cover abandonment, pending work, incomplete/inconsistent totals and
missing counters. The valid-record regression fails with the drain comparison
disabled and passes with it restored; this is **host-tested**, not live closure.

Fresh Win32 Release build and full CTest pass: 32 PASS, 1 SKIP, 0 FAIL. The new
mapped-image regression checks hash/bitness/call-site rejection, import conflicts,
page-protection restoration, original forwarding after owner join and repeat
callbacks. A real x86 DLL/process fixture reproduces skipped cleanup without
the hook and cleanup before original destroy/atexit with the production hook.
The GPU fixture explicitly shuts capture down after its pending D3D11 copy
drain and verifies both producer leases reclaimed with no active GPU ring.
RED->GREEN checks were observed for the adapter and production connection;
the verifier rejects otherwise-valid evidence missing pre-exit completion.

**Experiment-pending:** the actual CoJ/OpenVR close still needs a fresh short
manual native-stereo gameplay/normal-quit/finish run. This does not establish
that the previous live capture-cleanup failure is fixed. Required evidence:
pre-exit begin/end/restoration, `shutdown_complete=true`, drained pending copies,
zero abandoned leases, capture_end, transport/presenter summaries and run_end.
Cadence, per-eye visual pairing, reset with pending frames and device loss also
remain open. The durable exact-build contract is in CoJ's
`docs/research/COJ_SHUTDOWN_BOUNDARY.md`; raw investigation remains in ignored
`work/transport-diagnosis/`. No game/SteamVR was launched automatically. LTR
source/transport/temporal/backend work remains independent, with no CoJ dependency.

**Host-tested / staged, physical run pending:** independent review reports the
drain-acceptance finding resolved and no remaining concrete blocker in the
reviewed candidate. CoJ `prepare` freshly rebuilt and passed the full suite again
(32 PASS, 1 SKIP, 0 FAIL) and staged transport run
`20260930T230726Z-960ce616f954`, manifest
`732B92163EBA2699C8B0171C9916C4BAC732F784BC691B925FDE6BDB0AA1D654`.
Proxy SHA-256 `F4D47263C8E6BFD14AE394AF99B8EDBF13E1DF3FF890C008D1D6188C2D6405D9`.
Tracking/stereo enabled, Body IK disabled, reversible 1920x1080 / FSAA0 profile.
The next operator action is manual SteamVR/CoJ launch, brief gameplay and normal
menu quit, then CoJ `finish`. Do not prepare a second candidate while this one
is staged. Earlier “no candidate staged” statements below are historical.

**Live-tested production shared-stereo follow-up / host-tested verifier and
exit-lifecycle diagnosis (2026-10-01):** the separately authorized CoJ repository
was updated after the user's manual gameplay run
`20260930T220731Z-eb49cc77ac51`, manifest
`DE8E0E02485E15771ABE7FC219D302891CF7D958A2F388768C0EEBC5EFC839AF`.
The retained package SHA-256 was verified as
`E947A6AB8AB048ED814A7FDBAFD0081EA514D9EE917789DD9B03FB5ED4241D73`.
Real telemetry proves shared native-stereo publication, D3D11 consumer copy and
successful new-frame submissions to both OpenVR eyes with an explicit render
pose. Sampled native producer CPU readback/copy and producer/consumer waits are
zero. The operator reports no slowdown (**observed**, not performance-validated).
The monoscopic clip shows gameplay and avatar occlusion; it does not establish
stereo pairing or body/IK acceptance, which this profile disables.

**Implemented / host-tested:** CoJ's verifier previously required
`pose_mode=explicit_render_pose;content=new` adjacency, but production inserts
`presentation_mode=native_stereo`. Its corrected predicate and realistic fixture
pass and explicitly reject flat mode, zero pose, a failed eye and repeats only.
Fresh Release CTest: 30 PASS, 1 SKIP, 0 FAIL. Frozen replay with exact retained
artifact hashes passes that predicate and now fails at missing owner shutdown.
No game or SteamVR was launched; no candidate remains staged.

**Observed open blocker:** `shutdown_complete=false`, no worker cleanup tail,
and finalization stops at `native_stereo_shutdown: stage=capture_begin` without
transport totals or `run_end`. Missing totals are unknown, not zero. A minimal
x86 MSVC 19.44 DLL/host probe reproduces owner cleanup being skipped when stop/join
is called from DLL atexit after ordinary process exit; explicit pre-exit stop/join
completes owner cleanup. This is **host-tested** only. **Hypothesis:** the mod's
DLL atexit lifecycle accounts for its killed presenter; the exact capture-cleanup
failure remains unproved. Identify a demonstrated pre-exit boundary and test its
ownership before repeating physical acceptance; do not mask the failure by
promoting join completion to runtime shutdown. Windows ordering references:
[ExitProcess](https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-exitprocess),
[DLL best practices](https://learn.microsoft.com/en-us/windows/win32/dlls/dynamic-link-library-best-practices).
Raw evidence/replay/clip samples and reproducible host probe are under CoJ's
ignored `work/transport-diagnosis/`. This supersedes the pending shared transport
statement below only for the demonstrated publication/copy/submission boundary.
Full transport acceptance, clean shutdown, measured cadence, reset with pending
frames, device loss and visual stereo pairing remain open. LTR temporal inputs,
reconstruction, result return and XeSS integration are not promoted by this run.

**Live-tested / vr-headset-validated production startup acceptance
(reported 2026-10-01):** the user completed the corrected CoJ candidate
`20260930T220209Z-7435a4c5f588` (manifest
`7B4E4A29B9823363851DBCB3303B511AF67D5EC6C97069BF73CC52994C899C0A`).
The retained package hash was freshly verified as
`BF4DE502BB172C969F5B49A37080F5B0EB59F827564DC40BBC8A71DB9A66A320`.
The startup verifier passes; the operator saw videos, the main menu and a VR
pointer inside the visor. The summary records 1,098 uploads/new submissions,
667 repeats, zero submission failures, no frame rejections and presenting=true.
Actual PS VR2 optics match the asymmetric host fixture: neutral eye FOVs
left `(-1.07338,0.758284,0.925725,-0.925725)` radians, right mirrored horizontally;
eye offsets +/-0.032 m, identity rotation. The corrected texture is 2893x2860
for the 1920x1080 source, with left/right x offsets 973/0. This confirms the
previous fixed-extent clipping cause and supersedes pending corrected-startup
statements below. Production startup is live-tested, and startup visibility is
vr-headset-validated for this path/host; pointer precision/usability is unproved.

Shared resource opens/copies and stereo captures are zero because this was
StartupOnly's CPU-composed flat path; zero-valued shared timing samples do not
validate GPU sharing or its performance. Inner presenter shutdown still reports
false although the outer proxy finalizes and finish restores staging/video.
The next CoJ gate is fresh `prepare` without StartupOnly/BodyIkAtStart, brief
native-stereo gameplay with head motion and ordinary movement, normal close and
`finish`; retain the known inner-shutdown failure as an independent open gate.
No candidate is currently staged. Raw package/log and operator observations
are recorded under CoJ's ignored `work/startup-presentation-diagnosis/`.
LTR temporal-input/backend work remains independent and unpromoted by this run.

**Live-tested production follow-up / host-tested presentation correction
(2026-09-30):** the user manually ran the separate CoJ production candidate
`20260930T201221Z-c73e554d0695` and completed `finish`. Its retained package
SHA-256 is `223FEF62508AF096F47601FB1B5BF5D690A640F4081CAE0184D04756273E22EB`.
The exact game kept Ex factory/device identity, sustained successful Presents
beyond frame 2,970, completed Reset into generation 2 and continued flat capture.
All 380 logged translated texture results (332 2D, 48 cube) succeeded; VB/IB
calls have no per-call production telemetry. Outer hooks restored and run_end
was emitted. This supersedes the earlier pending manual production startup/reset
statement only for that compatibility boundary. It does not establish resource
content preservation or native stereo transport in the mod.

The full production startup gate still failed: all 1,513 mailbox frames were
rejected at flat-theater eye-plane placement, with zero uploads/submissions and
inner presenter shutdown incomplete. A host regression reproduces rejection
using the existing PS VR2 asymmetric-optics fixture, the logged 2804x2860 eye
recommendation and 1920x1080 source. Fixed 35% padding cannot contain the image
around its asymmetric projected center. The separately authorized CoJ tree now
derives a shared texture extent from both optical centers/FOVs, retains prior
padding/recommendations as minima, rejects invalid/oversized allocations and
logs initial eye optics for the next run. RED->GREEN regression and fresh Release
CTest pass (30 PASS, 1 SKIP, 0 FAIL). This fix is host-tested only; the frozen
physical log has no per-eye FOV/rotation values, so exact live geometry still
requires confirmation. Raw diagnosis is under CoJ's ignored
`work/startup-presentation-diagnosis/`. Game staging is restored; do not launch
the game/SteamVR automatically. Next CoJ gate: a fresh manual
`prepare -StartupOnly` -> videos/menu on a flat screen inside the visor -> normal
close -> `finish`. LTR temporal-input/backend work remains independent.

1. Treat the observed D3D9Ex startup and reset-content path as live-tested for this build/host, not as generic MANAGED emulation. Keep the semantic adapter bounded to the observed resource classes and gather the next source-compatibility evidence only when a concrete failure appears. Device-loss behavior and non-lockable resources remain open; shadow/recreation logic requires evidence that the game actually needs it.
2. The prepared real multiframe/reset lifecycle gate is live-tested and the targeted host-side synthetic failure-hardening tranche is complete. Extend it only when real integration exposes a concrete need: consumer loss/capture failure, repeated reset/device-loss, stale-result rejection or a new lifecycle hazard. Keep content-validation scope separate from fence/lifecycle success.
3. Extract only the internal frame/lifecycle semantics needed for the vertical experiment: monotonic resource/config generation, frame identity, camera/view identity, eye identity where applicable, reset/history validity, render/output extents plus valid subrects, depth convention, jitter, MV scale/convention, frame delta and backend inputs. Pair asynchronous temporal data by generation/frame/view and reject stale or ownership-mismatched inputs. Keep it internal; do not freeze a public ABI.
4. Extend the real observer only as needed for scene-color/depth/pass/HUD/reset semantics, and replace probe-style global assumptions with per-device/per-swapchain state and fail-open behavior. Explicitly validate whether the D3D9Ex substitution changes reset/lost-device behavior relevant to Chrome Engine.
5. Feed the resulting real `TemporalFrame` into XeSS Native AA 1:1 as the first reconstruction path, with explicit history validity/reset semantics.
6. Measure the real route before optimizing it: D3D9 event-query stalls, GPU copies, buffering/backpressure, allocations, logging, frame pacing and latency. Change transport mechanics only when those measurements expose a material problem.
7. Resume backend comparison after that gate: DLAA/DLSS-compatible mapping, FidelityFX and then SR. Treat renderer-resolution control, mip bias, HUD/post effects, particles and secondary cameras as separate SR integration work.
8. Expand D3D10 and D3D8 after the vertical contract has proved reusable. Preserve per-eye identity/history throughout, but defer OpenXR graphics-session/submission work until the flat real slice is stable; rerun the bootstrap when an HMD is visible.

## Reference snapshots

- BioShock VR DLSS/DLAA: `v0.2.17-en` / `8671fc87c4646140419ea64bd6e60d59fcac4723`.
- Rogue Trader EnhancedGraphics: `v2.2` / `01b1cd816db08f2b1c6c68b1319c6f44dd61bdd6`.
- W40KRT_VR current reviewed beta: `v0.9.81-beta` / `0fb98a2f991075256dffd2117f9d458360caa324` (published 2026-09-24T22:51:18Z); first reviewed beta `v0.9.79-beta` / `929bc9c1626b92d102def4ab405bf943c83bab24` retained for historical comparison.
- OFXR Bridge case study: `dad56acafc6e1dde219940427738b926cf2ea555`.
- XeSS SDK: `v3.0.2` / `8fe81bd`.
- AMD FSR SDK: `2.3.0`; inspected signed DX12 binaries are x64.

## Working constraints

Keep source-API adaptation, temporal-data production, transport and reconstruction backend separable until experiments justify coupling them. Preserve per-eye resource/history identity as an architectural requirement. Treat current probes as evidence generators rather than production modules. Do not begin a production injector or freeze a universal public temporal ABI during the current research phase.
