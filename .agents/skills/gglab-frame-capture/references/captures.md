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
