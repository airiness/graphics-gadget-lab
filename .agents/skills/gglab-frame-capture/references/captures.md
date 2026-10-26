# Captures and comparison

For one image, launch the selected build hidden from the repository root:

```powershell
$captureOutput = Join-Path (Get-Location) 'Build/Validation/FrameCapture/atrium-vulkan'
Build/Output/x64/Develop/GraphicsGadgetLab.exe --rhi vulkan --demo atrium --hidden `
    --capture-on-ready $captureOutput `
    --capture-settle-frames 16 --capture-view CAM_Courtyard
```

`--capture-on-ready` requires an absolute directory; `--capture-view` is optional.
Output reports `capture-admission`/reason, accepted `capture-job`, `capture-status`,
`capture-publication` and `capture-manifest`, plus convenience image/metadata paths.
Success requires Completed + Published and the manifest. Exit codes are 0 published,
2 failed/cancelled/rejected, 3 timed out. `--capture-timeout` defaults to 120 seconds
from the first rendered frame.

Busy retries the original plan under that deadline without inventing a job id.
Timeout after acceptance requests whole-job cancellation; `TooLate` does not
promise publication was prevented. Late publication cannot rewrite the timeout
outcome. The startup owner consumes its receipt, including late shutdown results;
shutdown bounds pending writing to 30 seconds. Native regression:
`Tests/Verification/RunVerification.ps1 -Suite capture-on-ready` (all configurations, both RHIs).

## Session settings

Use `$session = 'Scripts/Verification/GGLabSession.ps1'` and an existing session id.
`status.frame.referenceViews` lists authored views; omit `-View` when none exist.
`batch` captures all views or a `-Views` subset in order, consuming one job before
the next; each view change is a camera cut and restarts settling.

| Option | Meaning |
| --- | --- |
| `-Source scene` | Default: post-processed scene without tooling UI/debug overlays. |
| `-Source composited` | Presented image, including active overlays and DevTools. |
| `-Source diagnostic -DiagnosticTap <tap>` | Display-resolution preview; fails if no tap was produced that frame. |
| `-Timing after-ready` | Default: all gates ready, then `-SettleFrames` submitted frames (default 8). |
| `-Timing next-frame` | Next eligible frame, including while loading. |
| `-RequiredContentId <id>` | Require this active Lab or Demo. |
| `-RequiredFrameState` / `-RequiredFrameStateFile` | Require typed values and temporal state before issue; see [frame predicates](session-jobs.md#required-frame-state). |
| `-OutputDirectory <path>` | Override `Build/Sessions/<id>/Captures/`. |
| `-Label`, `-Note` | File subject and metadata annotation. |
| `-NoWait`, `-KeepResult` | Admission-only or retained-result operation; see [session jobs](session-jobs.md). |
| `-Products` / `-ProductsFile` | Ordered required products from one frame; see [session jobs](session-jobs.md#submit-required-products). |

Hidden runs default to fixed 1/60 s simulation. Settling restarts on camera cuts
or history resets; loading history resets before settling. Use at least eight
frames for temporal effects, more for convergence. Inspect pending/failed gates
in `status` when a capture is waiting.

Find diagnostic taps in
[PostProcessDebug.h](../../../../Sources/GGLabRuntime/Public/GGLabRuntime/Graphics/PostProcess/PostProcessDebug.h),
including `temporal-history-weight`, `temporal-rejection`, depth and `gtao-*` taps.
One frame records one tap; another tap can delay an ordinary capture. Each attempt
keeps its issued frame's evidence. A pre-submit Busy result can retry the plan on a
new frame, rechecking its predicate. Use the recorded frame and attempt to identify
the result. For oversized requests, reduce resolution or split observations into
separate jobs.

## Inspect output

Use the returned `manifest` and `products[].files`; the
[artifact contract](session-jobs.md#interpret-results-and-artifacts) defines
publication, schema versions and provenance. Each PNG's same-stem JSON sidecar
records source/content/backend, view/camera, frame serial/index, resolution,
fixed step/time, readiness, DevTools, temporal settings, frozen parameters and
control state, plus applicable sequence/watch metadata. Read the saved metadata
and logs alongside the image.

## Compare images

Keep content/view/settings and labels matched across DX12/Vulkan or before/after
arms. Use fresh output directories to avoid selecting older captures.

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/Verification/CompareCaptures.ps1 `
    -Reference Build/Sessions/atrium-dx12/Captures `
    -Candidate Build/Sessions/atrium-vulkan/Captures `
    -DiffDirectory Build/Validation/FrameCapture/atrium-diff
```

The script accepts images or directories. Directories pair PNGs through sidecars
by content/view/source/label, selecting the newest per key. Invalid sidecars or
wrong `image.file` names reject; other export/receipt JSON files are ignored.

Inspect `metadataDifferences` first: camera, resolution, temporal settings and
simulation time can explain pixel changes. Backend readiness can differ; align
moving content by sequence frame or another controlled state. Backend/request
and capture-time identities are expected to differ.

Metrics are mean/max absolute 8-bit RGB channel error, percentage of pixels above
`-Threshold` (default 8), and PSNR. `-MaxMeanError`/`-MaxDifferingPercent` enforce
tolerances; exit 1 also reports missing pairs or size mismatch. Metadata differences
alone do not fail. Inspect both images and amplified differences; choose tolerances
for the intended claim rather than assuming bit identity. GPU-free regression:
`Tests/Verification/TestCompareCaptures.ps1`.

## Guarded TAA comparison

`Invoke-TaaComparison.ps1` runs the static lighting-contract fixture in one fresh
hidden automation session. It pins TAA's Native preset, captures TAA Active and
Disabled arms with 32 settled frames each, and requires one Published SameFrame
image/full-graph bundle per arm. The graph checks `PostProcess.TemporalAA` against
the recorded executed pass indices. It then restores the original Host override
presence/value and captures a submitted restoration observation.

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/Verification/Invoke-TaaComparison.ps1 `
    -Configuration Develop -Rhi dx12 -Python python
powershell -NoProfile -ExecutionPolicy Bypass -File Tests/Verification/RunVerification.ps1 `
    -Suite taa-comparison -Configuration Develop,Release -Rhi dx12,vulkan -Python python
```

Python needs `jsonschema`; select an installed environment with `-Python`.
`-OutputDirectory` creates a unique run child; `-RunDirectory` names an exact new
directory. `-TimeoutSeconds` bounds each client wait. Read `result.json`,
`journal.json`, `comparison.json`, the session log and the published products.
The journal retains requested operations, confirmed handles, receipts and chained
control stamps. `cleanup.intentRestored`, `effectsSucceeded` and
`renderingVerified` are separate claims.

By default the runner creates and stops only its own matching run. `-Attach
-Session <id>` can use an existing automation session only if its fixed fixture,
640x360 extent, fixed step and disabled DevTools pass preflight/capture checks;
it never automatically stops an attached process. Developer/F5 lifetime and
suspended responsiveness remain separate work.

`RestoreConflict` leaves a concurrent writer's state intact, reporting remaining
differences. Content replacement reports cleared overrides or currently present
overrides whose retention ownership discovery cannot establish; it never restores
across the activation change.
Lost admission or consumption is uncertain: do not replay a mutation or consuming
query from the journal. Recover confirmed handles only against the recorded
`runIdentity`, using [session jobs](session-jobs.md#run-checked-requests) and
[parameter results](session-parameters.md). Terminal evidence is saved before
consumption. On capture timeout the runner cancels its owned job and reads the
actual terminal result: `TooLate` can still publish or fail, and an unconfirmed
cancellation never proves publication was prevented.

The comparer accepts `-AllowedDifferencePolicy <file>`. The shipped
`TaaComparison.policy.json` declares the TAA differences and static time mode.
Unknown paths/wildcards reject; consumers compare by name, Host view entries by
id, and the temporal service union is checked. Both metadata reporting and
`-RequireMatchingInputs` use the same exact permissions, including explicit
static-time and IBL-publication exceptions. Image size and render extent must
match. The runner reports pixel metrics without pixel acceptance thresholds;
this experiment proves the control/evidence loop, not TAA image quality.

GPU-free suites `capture-comparison` and `taa-comparison-workflow` cover comparison
policies, cancellation verdicts, lost responses, conflicts, ownership and required
graph evidence. GPU suite `taa-comparison` supports Debug/Develop/Release on both
RHIs; each configuration needs its own built executable.
