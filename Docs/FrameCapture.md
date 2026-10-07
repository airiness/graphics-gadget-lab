# Frame capture for background verification

GraphicsGadgetLab can render without showing a window and write captured frames
as PNG files with a JSON metadata sidecar. Agents use this to verify rendering
changes without disturbing the desktop. A capture shows what one content state
rendered; inspect the images and metadata before claiming visual correctness.

All commands run from the repository root after building the configuration
they use (Debug by default):

```powershell
$session = 'Scripts/GGLabSession.ps1'
```

## Choosing a workflow

| Need | Use |
| --- | --- |
| One image of one Lab or Demo | `--capture-on-ready` one-shot launch |
| Several images, views or settings from one process | A session (`GGLabSession.ps1`) |
| Every authored camera view of the content | `GGLabSession.ps1 batch` |
| DX12 and Vulkan agreement | Two sessions, `batch` on each, `CompareCaptures.ps1` |
| Temporal behavior along a fixed camera motion | `GGLabSession.ps1 sequence` |

Sessions and one-shot launches are hidden by default: the window is never shown
or activated, input is ignored and simulation advances with a fixed 1/60 s step.
Use `-Visible` only when the owner allows windows on the desktop.

## One-shot capture

```powershell
Build/Output/x64/Debug/GraphicsGadgetLab.exe --rhi vulkan --demo atrium --hidden `
    --capture-on-ready D:/captures --capture-settle-frames 16 --capture-view CAM_Courtyard
```

The process waits until the content is ready, renders the settle frames, writes
the capture and exits. It prints `capture-status`, `capture-image` and
`capture-metadata` lines, or `capture-failure`, `capture-pending-gate` and
`capture-settled-frames` lines when the capture fails or times out. Exit codes:
0 written, 2 failed, 3 timed out (`--capture-timeout`, default 120 seconds).
`--capture-view` is optional; see [Reference views](#reference-views).

## Sessions

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File $session start -Session atrium -Rhi dx12 -Demo atrium
powershell -NoProfile -ExecutionPolicy Bypass -File $session status -Session atrium
powershell -NoProfile -ExecutionPolicy Bypass -File $session capture -Session atrium -View CAM_Courtyard -SettleFrames 16
powershell -NoProfile -ExecutionPolicy Bypass -File $session batch -Session atrium -SettleFrames 16
powershell -NoProfile -ExecutionPolicy Bypass -File $session stop -Session atrium
```

- `start` launches the process and returns once it accepts requests. Select
  content with `-Lab <gglab.lab.*>` or `-Demo <start|island|atrium|lab>`, the
  backend with `-Rhi`, and the render size with `-WindowSize 1280x720`.
- `status` reports the active Demo and Lab, readiness gates, settled frames,
  camera, reference views and pending captures.
- `capture` waits for the written files unless `-NoWait` is given; `result
  -RequestId <id>` then reports it.
- `batch` captures every reference view the content reports, or `-Views a,b`,
  in that order, and waits for all of them.
- `stop` ends the session and reports the process exit code. Always stop
  sessions you started; an idle session exits after `-IdleTimeout` seconds
  (default 900).
- `list` shows running sessions.

Every command prints one JSON object. Exit code 1 means the command itself
failed (for example, no such session); a capture that ran but failed still exits
0 with `"status": "failed"` and a `failure` reason.

Session files live in `Build/Sessions/<id>/`: `output.log` holds the process
output, and captures go to `Captures/` unless `-OutputDirectory` is given.

## Capture settings

- **Source.** `scene` (default) is the post-processed image without IBL
  previews, the always-visible debug overlay or tooling UI. `composited` is the
  final presented image, including overlays and DevTools UI.
- **Timing.** `after-ready` (session default) waits until every readiness gate
  is ready and then for `-SettleFrames` submitted frames with an unchanged
  settle key (temporal session, camera cut, display view, size and Demo).
  `next-frame` captures the next recorded frame, even while loading.
- **Readiness gates.** `shaders`, `content-transition`, `content`, `lab`,
  `environment`, `ibl` and `asset-uploads`. A pending gate keeps an
  after-ready capture waiting; `status` shows its detail. A failed gate fails
  the waiting after-ready captures of that content at once, with the gate's
  detail as the reason; submit them again after fixing the cause.
- **Content.** `-RequiredContentId` additionally waits until that Lab or Demo
  id is active.
- **Label and note.** `-Label` names the files and is stored with `-Note` in the
  metadata.

Settle at least 8 frames for temporal effects, and more after a camera cut or
for content that keeps converging.

## Reference views

Content may register camera reference views: authored main-camera poses such as
`CAM_Courtyard` for CoastalAtrium. `status` lists them under
`frame.referenceViews`. A capture with a view restores that pose before it is
captured:

- Views are applied in submission order. A view is restored only after every
  earlier request was issued, so queued captures keep their own camera.
- Restoring a view is a camera cut that restarts settling, so `after-ready`
  captures render their settle frames under the view.
- An after-ready view waits for ready content, which registers its views. A view
  the content does not register fails with the available ids.
- If something else cuts the camera before the capture, the view is restored
  again.

The DevTools capture panel offers the same views and a "Capture All Views"
button for interactive use.

## Output

Each capture writes
`<subject>[-<view>]-<source>-<backend>-<UTC time>-r<request id>.png` and a
`.json` sidecar with the same stem. The subject is the label, else the Lab id,
else the Demo id. A name that already exists, for example from another session
writing to the same directory, is never replaced: the capture takes the next
free suffix (`-2`, `-3`, ...), and its sidecar's `image.file` names the PNG it
belongs to.

The dedicated writer thread runs one job with at most eight pending jobs. A new
capture fails with `Capture writer queue is full.` when the pending queue is full.

The PNG and JSON sidecar are each written completely to temporary `.partial`
files before publication. They are published separately, so either file may
appear before the pair is complete. When the process shuts down it waits for
unfinished writing up to 30 seconds in total. Captures still unfinished then
fail: jobs that have not claimed publication are abandoned and never publish
their files. Jobs that already claimed publication have an indeterminate
outcome; their files may already exist or appear later, despite the failed result.

The sidecar (`schemaVersion` 1) records the request (label, note, source,
timing, settle frames), backend, Demo and Lab ids, frame serial and index, image
size and display format, camera pose and `referenceView`, fixed time step and
total time, whether DevTools were active, every readiness gate, and the UTC
capture time.

## Sequences

Content may register camera paths: versioned main-camera motions whose pose is
a function of the sequence frame only, such as `SEQ_DollyDoorway` for the coastal
retreat (`-Demo atrium`). A sequence plays one path and captures the requested
frames:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File $session sequence -Session atrium `
    -CameraPath SEQ_DollyDoorway -CaptureFrames '0,90,179' -Label dolly-taa
```

- Frame 0 starts after a rendered frame reports every readiness gate ready.
  Each submitted frame then advances the path by exactly one frame; a frame that
  ends without submission is posed again.
- Frame 0 and every cut key of the path are camera cuts that reset temporal
  history and the jitter sequence, so a replay starts from the same temporal
  state. Any other temporal continuity change (temporal session, display view,
  size or content) or a readiness gate leaving Ready fails the sequence.
- Each requested frame is captured on exactly that frame as a next-frame capture
  labelled `<label>-f<frame>`; the label defaults to the path id. At most eight
  captures may wait for the writer, so capture sparse frames until sequence
  capture applies back-pressure.
- `sequence` waits until the sequence and its captures finish unless `-NoWait`;
  `status` reports the active or last sequence, and `sequence-cancel` stops it.
  One sequence runs at a time; avoid submitting capture views while it runs.

Replays are deterministic only for content whose state depends on the sequence
frame alone. Check it by playing the same path twice and comparing the captures.

## Comparing captures

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File $session start -Session atrium-dx12 -Rhi dx12 -Demo atrium
powershell -NoProfile -ExecutionPolicy Bypass -File $session batch -Session atrium-dx12 -SettleFrames 16
powershell -NoProfile -ExecutionPolicy Bypass -File $session stop -Session atrium-dx12
# Repeat with -Rhi vulkan and session atrium-vulkan, then:
powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CompareCaptures.ps1 `
    -Reference Build/Sessions/atrium-dx12/Captures -Candidate Build/Sessions/atrium-vulkan/Captures `
    -DiffDirectory Build/Sessions/atrium-diff
```

`CompareCaptures.ps1` compares two images, or two capture directories matched
through their sidecars by content, reference view, source and label (the most
recent capture per key). It reports per pair the mean and maximum absolute
8-bit channel difference, the percentage of pixels above `-Threshold` (default
8) and PSNR, and writes amplified difference images when `-DiffDirectory` is
given. `-MaxMeanError` and `-MaxDifferingPercent` turn it into a check: exit
code 1 when a pair exceeds them, sizes differ or a capture has no counterpart.

Sidecars with an unsupported `schemaVersion` are listed under `rejected` and
not compared, which also makes the exit code 1. Each pair lists the frame
settings that differ between its sidecars under `metadataDifferences`: camera,
time step, total simulated time, size, content and the like. Backends, request
ids and capture times are expected to differ and are not listed. Check these
before reading pixel differences as rendering differences. Sessions on
different backends can become ready a few frames apart, so `time.totalTime`
often differs; for content that changes over time, part of the difference then
comes from the scene itself.

DX12 and Vulkan are not bit-identical. Small mean errors with isolated
differing pixels are expected; look at the difference images for structured
differences such as shifted shadows, missing passes or color shifts.

## Troubleshooting

- **`start` reports the session did not become ready:** its result includes the
  tail of `output.log`; the full log is in the session directory.
- **An after-ready capture stays queued:** run `status` and look for a pending
  gate, or a `requiredContentId` that is not active.
- **`Frame capture N has not finished encoding` in the log:** writing the PNG or
  sidecar is blocked, for example by security software protecting the output
  directory.
- **Unknown reference view:** the failure lists the views the active content
  registers; Labs without authored views register none.
