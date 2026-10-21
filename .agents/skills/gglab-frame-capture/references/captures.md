# Captures and comparison

Run from the repository root after building the selected configuration.
For a single image, a hidden one-shot launch captures and exits:

```powershell
$captureOutput = Join-Path (Get-Location) 'Build/Validation/FrameCapture/atrium-vulkan'
Build/Output/x64/Develop/GraphicsGadgetLab.exe --rhi vulkan --demo atrium --hidden `
    --capture-on-ready $captureOutput `
    --capture-settle-frames 16 --capture-view CAM_Courtyard
```

`--capture-on-ready` requires an absolute directory. One-shot output
reports `capture-status`, `capture-image` and `capture-metadata`, or failure and
pending-gate details. Exit codes are 0 written, 2 failed and 3 timed out.
`--capture-timeout` defaults to 120 seconds. `--capture-view` is optional.

## Session settings

Set `$session = 'Scripts/Verification/GGLabSession.ps1'` and use an id you started in place of
`<id>`. `status` lists authored views in `frame.referenceViews`. Omit `-View` for
content without them. `batch` captures all reported views, or a `-Views` subset,
in order; each view change is a camera cut and restarts settling.

| Option | Meaning |
| --- | --- |
| `-Source scene` | Default: post-processed scene without tooling UI/debug overlays. |
| `-Source composited` | Presented image, including active overlays and DevTools. |
| `-Source diagnostic -DiagnosticTap <tap>` | Display-resolution diagnostic preview; fails if the feature produced no tap that frame. |
| `-Timing after-ready` | Default: all readiness gates ready, then `-SettleFrames` submitted frames (default 8). |
| `-Timing next-frame` | Next eligible frame, including while loading. |
| `-RequiredContentId <id>` | Also require the specified active Lab or Demo. |
| `-OutputDirectory <path>` | Override `Build/Sessions/<id>/Captures/`. |
| `-Label`, `-Note` | File subject and metadata annotation. |
| `-NoWait` | Retrieve with `result -JobId <id>`; release a terminal receipt with `-Consume`. |
| `-KeepResult` | Wait without consuming; otherwise a waiting client explicitly consumes the terminal receipt. |
| `-Products` / `-ProductsFile` | Ordered typed required products; see [session jobs](session-jobs.md). |

Hidden runs use a fixed 1/60 s simulation step unless overridden. Settling
restarts after camera cuts or temporal-history resets; loading history is reset
before settling. Use at least 8 frames for temporal effects, more for convergence.
Pending/failed readiness gates appear in `status`; failed gates fail waiting
captures. Do not repeatedly queue captures to bypass a failed gate.

Useful diagnostic taps include `temporal-motion-direction`,
`temporal-motion-magnitude`, `temporal-history-color`, `temporal-reprojection-uv`,
`temporal-rejection`, `temporal-history-weight`, `temporal-history-samples`,
`temporal-clip-distance`, `temporal-history-relaxation`, `scene-depth-raw`,
`scene-depth-linear-view-z` and `gtao-*`. Consult
[PostProcessDebug.h](../../../../Sources/GGLabRuntime/Public/GGLabRuntime/Graphics/PostProcess/PostProcessDebug.h)
for tap declarations. A frame records one tap; another tap can delay an
ordinary capture, so compare the recorded frame/time rather than queue order.

## Output and completion

Session v3 results identify a final `manifest` and ordered `products[].files`,
not flattened `image`/`metadata` fields. Require `publication: Published` as well
as `status: completed`; [session jobs](session-jobs.md) defines admission,
retention, cancellation and the remaining sequence/interactive migration.

Each PNG has a same-stem JSON sidecar (`schemaVersion: 1`). Use the paths returned
by the command, rather than predicting filenames. The sidecar records the PNG
name, source, content, backend, reference view, camera, frame serial/index, size,
fixed step/total time, readiness, DevTools, temporal settings and optional sequence
and watch metadata. Existing files receive a free suffix instead of replacement.

PNG and sidecar publish separately after temporary `.partial` writes. Wait for
the command's completed result before consuming them; file existence alone is
insufficient. Shutdown bounds pending writing to 30 seconds; a late file does
not turn a failed result into a successful capture.

Captures and diagnostic exports share bounded requests, payload slots and bytes.
Session batches submit and consume one view at a time; admission Busy is explicit.
Sequences still use their transitional capacity-wait path before advancing a frame. Unsupported or oversized requests fail explicitly; reduce
resolution or split an optional group rather than retrying the same oversize
request. An issued capture never substitutes a later frame after an abort.

## Compare images

Capture equivalent views/settings on DX12 and Vulkan, or before and after a
change. Keep labels identical across arms, and use fresh output directories to
avoid selecting an older capture by accident.

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/Verification/CompareCaptures.ps1 `
    -Reference Build/Sessions/atrium-dx12/Captures `
    -Candidate Build/Sessions/atrium-vulkan/Captures `
    -DiffDirectory Build/Validation/FrameCapture/atrium-diff
```

The script accepts individual images or directories. Directories pair PNGs
through sidecars by content, view, source and label, choosing the most recent
capture per key. Missing/malformed/unsupported sidecars and wrong `image.file`
names are rejected; session receipts and other export JSON files are ignored.

Inspect `metadataDifferences` before interpreting pixel differences. Camera,
resolution, temporal settings or simulation time can change the image. Different
backends may become ready on different frames; moving content requires aligned
sequence frames or another controlled state. Backend/request/time-of-capture
identities are expected to differ.

Metrics are mean/max absolute 8-bit RGB channel error, percentage of pixels
above `-Threshold` (default 8), and PSNR. `-MaxMeanError` and
`-MaxDifferingPercent` make tolerances enforceable; exit 1 also reports missing
pairs or size mismatch. Metadata differences alone do not fail the comparison.
Inspect both images and amplified differences for shifted edges, missing passes
or color changes. DX12/Vulkan need not be bit-identical; set tolerances according
to the intended claim. The GPU-free regression gate is
`Tests/Verification/TestCompareCaptures.ps1`.
