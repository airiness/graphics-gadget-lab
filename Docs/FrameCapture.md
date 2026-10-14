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
| Interactive screenshot or graph export in an ordinary/F5 launch | DevTools **Application > Frame Capture** |
| One image of one Lab or Demo | `--capture-on-ready` one-shot launch |
| Several images, views or settings from one process | A session (`GGLabSession.ps1`) |
| Every authored camera view of the content | `GGLabSession.ps1 batch` |
| DX12 and Vulkan agreement | Two sessions, `batch` on each, `CompareCaptures.ps1` |
| Temporal behavior along a fixed camera motion | `GGLabSession.ps1 sequence` |
| Pass order, versions, barriers and actual transient allocations | `GGLabSession.ps1 graph` |

Sessions and one-shot launches are hidden by default: the window is never shown
or activated, input is ignored and simulation advances with a fixed 1/60 s step.
Use `-Visible` only when the owner allows windows on the desktop.

## Interactive capture and graph export

For a normal visible launch, including F5 in Visual Studio or VS Code, open
DevTools **Application > Frame Capture**. No `--session`, PowerShell command or
control connection is needed. Keep DevTools enabled (`--no-devtools` removes the
panel).

- **Capture (F9)** writes the selected Scene or Composited PNG and its JSON
  metadata sidecar. F9 remains a screenshot hotkey; exporting a graph does not
  change its Source setting. When ImGui owns keyboard input, use the button or
  return focus to the rendered view before pressing F9.
- **Export Graph** writes the full compile and execution records described
  [below](#render-graph-export).
- **Export Graph Summary** writes the executed pass order and versioned
  reads/writes to JSON, without the execution record.
- Both export buttons share **Timing**, **Settle Frames**, **Reference View**,
  **Output Directory**, **Label** and **Note** with screenshots. Timing defaults
  to **Next frame**: the next rendered frame, not the already displayed frame.
  Select **After ready** to wait for the readiness gates and settle frames.
  Scene/Composited Source only affects screenshots.
- The pending count and **Current Frame** readiness gates help explain waiting
  requests. **History** retains the latest 16 results, showing the captured frame
  serial and published PNG/JSON paths or a failure. A short notification and the
  application log also name the actual output file, including graph-only JSON.
- Click a path in History to copy it. Screenshots show both the PNG and metadata
  path; graph exports show their single JSON path. Path text and the clipboard
  use UTF-8; glyph availability follows the configured ImGui font.
- **Open Output Folder** on a successful result opens that result's actual
  directory through the Windows Shell. Editing Output Directory afterward does
  not redirect it. Opening is explicit, never automatic on completion, and is
  unavailable for unfinished/failed results or hosts without this capability.
  The adapter does not create directories or open files with their associated
  applications. Missing/inaccessible directories or Shell errors appear next to
  the button without changing the capture's completed status. Acceptance by the
  Shell does not guarantee that an Explorer window has appeared. The explicit
  open call is synchronous on the frame thread; slow/network paths can stall a frame.

The Windows adapter and `Shell32.lib` dependency belong only to WinApp;
AppRuntime/Runtime still handle scheduling, fenced completion and publication
without a Shell dependency. Hidden launches have no folder-opening callback.
Graph export does not add GPU observation nodes or queries; per-pass timing,
build identity and shader artifact identity retain their explicit unavailable
status. Disable observation tools for performance measurements. Interactive
exports include DevTools passes, unlike hidden sessions with DevTools disabled;
compare `frame.developmentTools` before comparing graph structure.

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

## Render-graph export

Sessions use control protocol **version 2**. Version 1 requests are rejected;
rebuild the application and use the matching session script together. The other
capture and sequence commands retain their behavior.

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File $session graph -Session atrium -Label frame-graph
powershell -NoProfile -ExecutionPolicy Bypass -File $session graph -Session atrium -Summary -Label discovery
# Optional: -Timing next-frame, -View CAM_Courtyard, -RequiredContentId,
# -SettleFrames, -OutputDirectory, -Note and -NoWait.
```

`graph` uses the existing capture request queue, readiness/settling rules and
bounded writer. It writes one JSON file, not a PNG. The response's `graph` and
`metadata` fields name that file; `result -RequestId <id>` retrieves a request
submitted with `-NoWait`. Files are named `<label>.<frameSerial>.graph.json`
or `<label>.<frameSerial>.graph-summary.json`, with a free suffix on collision.
Without a label the subject is the Lab or Demo id. Publication and shutdown
follow the capture writer's `.partial` and publication-claim rules.

The published schema is [`gglab.render-graph` v1](Schemas/RenderGraph.schema.json):

- `compile` is sampled before execution. Full mode contains declaration-order
  passes, name occurrences (1-based), culling, execution order, encoder,
  dependencies, accesses with resource versions, barrier intents, acquire/release
  lists, resource descriptors (including mip/slice counts and buffer size/stride),
  resource nodes, dependency edges and compile diagnostics. `duplicatePassNames`
  reports repeated names, including culled declarations. `occurrence` counts equal
  names in declaration order, including culled passes, not in summary execution
  order. Culling therefore cannot renumber a pass; T2/T3 addressing uses the same
  definition. Full accesses include `writtenResourceNodeIndex`/`writtenVersion`
  (null for read-only accesses). ReadWrite reads the incoming version and writes
  the new version whose writer is that pass.
- `execution` is a separate record. It contains executed pass indices, actual
  allocations with pool slot and physical handle index/generation, and resolved
  pre/post barriers keyed by pass index. A failed handle resolution is retained
  with `physical: null`, rather than silently disappearing. Allocation evidence is copied at
  `Devirtualize`, before `Release` clears the live allocation. Imported resources
  have no transient pool slot. Pool slots describe actual pool reuse, not a claim
  of native heap aliasing.
- `execution.gpuTiming` is explicitly `unavailable`: current backend profilers
  aggregate scopes by name and do not preserve individual pass identity. No merged
  duration is attributed to repeated passes, and graph requests do not enable GPU
  profiling or wait for a timing tail that cannot provide this evidence.
- Summary mode lists only executed passes, sorted by execution order, with `reads`
  and `writes`. Each entry includes its version, resource-node index, declared
  subresource range and an `address` object (`resource`, `at.before`/`at.after`,
  `occurrence`) for discovery. Resource export/watch addressing is introduced by
  T2/T3; these commands are not implemented yet.
- `frame` reuses capture metadata for content, camera, simulation time, render
  settings and readiness; `frame.frame.serial` equals the top-level `frameSerial`.
  It adds the Runtime adapter/driver compatibility identity. Build identity and
  per-pass shader artifact identity are explicitly unavailable with reasons;
  the capture service currently has no authoritative sources for these. The
  parameter revision is null until T4.
- `submission` records the fence handle index/generation and value. A successful
  graph result is published only after that exact fence completes. Aborted,
  failed or missing frames fail exact requests, never substitute another frame.
- `observation` flags the graph export. `observationNodes` is empty: T1 adds no
  RenderGraph nodes, resources, barriers or GPU queries. Recording is opt-in and
  inactive frames do not build a graph snapshot. Exports still add CPU/file work;
  performance runs must keep observation tools off.

Stable enum identifiers and UI labels share one table, but JSON uses only the
identifiers (for example `WriterToReader`, `DepthStencilWrite`, `CopySource`,
`AccessTransition` and `Uav`). Editing a UI label does not change schema v1.
Bit fields retain numeric values and join identifiers with ` | `; a null
subresource range means all subresources, and `"remaining"` in a range count means
Remaining. Physical handles are session-local evidence, not persisted asset ids.
T1 uses the TD12 unavailable GPU-timing fallback; identity-preserving per-pass
timing and sequence `graphFrames` scheduling are deferred. Existing sequence
`gpuTiming` is independent and remains available for measurement runs.

Validate published exports from **both** backend runs against the schema, not just
with a JSON parser (Python with `jsonschema` installed):

```powershell
python Scripts/ValidateRenderGraphExports.py Build/Sessions/atrium-dx12/Captures Build/Sessions/atrium-vulkan/Captures
```

The validator checks full/summary schema contracts and version/writer consistency.
Headless snapshot tests alone do not establish dual-backend GPU correctness.

## Capture settings

- **Source.** `scene` (default) is the post-processed image without IBL
  previews, the always-visible debug overlay or tooling UI. `composited` is the
  final presented image, including overlays and DevTools UI. `diagnostic`
  records one diagnostic tap named by `-DiagnosticTap`, rendered at display
  resolution with the post-process preview encoding: `temporal-motion-direction`,
  `temporal-motion-magnitude`, `temporal-history-color`,
  `temporal-reprojection-uv`, `temporal-rejection`, `temporal-history-weight`,
  `temporal-history-samples`, `temporal-clip-distance`,
  `temporal-history-relaxation`, `scene-depth-raw`,
  `scene-depth-linear-view-z` and the
  `gtao-*` taps. A tap whose feature produced nothing in that frame, such as a
  temporal tap with Temporal AA inactive, fails the capture.
  A frame records one diagnostic tap. Concurrent ordinary requests for the same
  tap may share that frame; a different tap waits before being issued, so its
  camera, time and temporal metadata come from the frame that records it.
- **Timing.** `after-ready` (session default) waits until every readiness gate
  is ready, then counts `-SettleFrames` successfully submitted frames. A content
  switch, camera cut, lost history or configuration change that resets temporal
  history restarts settling, even when the window size stays the same.
  History rendered while loading is reset before settling begins. Simulation
  time holds until then. Captures without active temporal effects still settle
  normally.
  `next-frame` captures the next eligible frame, even while loading. A conflicting
  diagnostic tap can delay an ordinary request before issuance.
- **Readiness gates.** `shaders`, `content-transition`, `content`, `lab`,
  `environment`, `ibl`, `asset-loads` and `asset-uploads`. A pending gate keeps
  an after-ready capture waiting; `status` shows its detail. A gate that leaves
  Ready restarts settling, but does not rewind simulation time already advanced.
  A failed gate fails the waiting after-ready captures of that content at once,
  with the gate's detail as the reason; submit them again after fixing the
  cause.
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

Capture capacity is bounded:

| Limit | Capacity |
| --- | --- |
| Captures awaiting GPU completion or file writing | 9 total; at most 8 wait behind the running writer |
| Runtime requests, including unconsumed results | 64 |
| Distinct captured images in flight | 8, sharing a 256 MiB raw-image budget |

The byte budget covers GPU readback and CPU image storage, excluding PNG output
and encoder working memory. An ordinary capture without capacity fails before
GPU submission. Sequences wait for temporary capacity pressure to clear; an
unsupported or oversized image fails explicitly.

Each issued capture stays bound to its recorded frame and metadata. If that
frame aborts, fails submission or does not record the requested source, the
capture fails; submit a new request to retry.

The PNG and JSON sidecar are each written completely to temporary `.partial`
files before publication. They are published separately, so either file may
appear before the pair is complete. When the process shuts down it waits for
unfinished writing up to 30 seconds in total. Captures still unfinished then
fail. Files already being published may still appear after that failure; use
the command result to determine whether the capture completed.

The sidecar (`schemaVersion` 1) records the request (label, note, source,
diagnostic tap, timing, settle frames), backend, Demo and Lab ids, frame serial
and index, image size and display format, camera pose and `referenceView`, fixed
time step and total time, whether DevTools were active, every readiness gate,
and the UTC capture time. `temporal` records the Temporal AA frame plan
(requested, status, disable reason), temporal session and reset identities, jitter index, sequence
length and offset in pixels, the resolved Temporal AA settings, and the render
and display extents. `sequence` names the camera path, its version, the
sequence frame and frame count, or is null outside a sequence.

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
  state. An unexpected history reset, loss of compatible history, content change
  or readiness gate leaving Ready fails the sequence. A path cut permits
  its camera reset and cold history, but does not permit simultaneous changes
  to resolution, accumulation model or active temporal effects.
- Each requested frame is captured on exactly that frame as a next-frame capture
  labelled `<label>-f<frame>`; the label defaults to the path id. Every frame of
  a path may be captured (`-CaptureFrames '0-179'`): when capture capacity is
  temporarily full, the session waits before the next requested capture frame,
  without advancing simulation time, the path or its reference sample. An
  unsupported or oversized target fails the sequence instead of deferring forever.
  If another diagnostic tap occupies an exact requested sequence frame, that
  capture fails rather than silently substituting a later path frame. An issued
  capture on an aborted frame also fails; a frame aborted before any capture was
  issued remains eligible for the normal pose replay.
- `sequence` waits until the sequence and its captures finish unless `-NoWait`;
  `status` reports the active or last sequence, and `sequence-cancel` stops it.
  One sequence runs at a time; avoid submitting capture views while it runs.

A sequence records one evidence channel per run: the scene, or one diagnostic
tap with `-Source diagnostic -DiagnosticTap <tap>`. Replay the path once per
channel; the frames correspond because the replay is deterministic.

### Supersampled reference

`-ReferenceSamples <n>` (1 to 4096) renders every sequence frame as a
supersampled reference instead of the production temporal path. Each frame is
rendered n times with Temporal AA inactive, the camera at the frame's pose and
simulation time held; every sample uses one Halton(2, 3) jitter phase (the first
eight equal the production TAA phases). Each sample contributes HDR scene color,
including transparent and depth-tested debug geometry. The mean of n samples is
post-processed for the captured image: a one-pixel box reconstruction filter.
Unlike the production path, transparent geometry is part of the accumulated image.

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File $session sequence -Session atrium `
    -CameraPath SEQ_StaticRailings -CaptureFrames 0 -ReferenceSamples 256 -Label static-ref
```

`-ReferenceTextureLodBias <b>` adds b to the material texture LOD of every
reference sample. The default 0 filters textures for the whole pixel before the
samples are averaged; `-0.5 * log2(n)` filters each sample for its own sub-pixel
footprint instead, which keeps texture detail that a pixel-sized prefilter removes.

A reference renders n frames per sequence frame for the whole path; capture
the frames you need and cancel with `sequence-cancel` once their captures
finished. Choose n by comparing references of n and 2n samples. Sidecars record
`sequence.referenceSamples`, and `temporal` shows Temporal AA as disabled with
the reference jitter index and sample count.

### Temporal AA evaluation

`-TemporalAA "name=value,..."` replaces display-view Temporal AA settings for
every frame of the sequence, so one replay evaluates one configuration from the
history reset at frame 0. Omitted settings keep the active content's values.
Built-in defaults are listed for reference; a Lab or Demo may supply different
values. Numeric ranges include both endpoints; out-of-range values are rejected
rather than clamped.

| Setting | Accepted values | Built-in default | Meaning |
| --- | --- | --- | --- |
| `enabled` | `true`, `false` | `false` | Request Temporal AA. |
| `maxHistoryFeedback` | 0 to 0.99 | 0.97 | Maximum history feedback. |
| `depthAbsoluteThreshold` | 0 to 1 | 0.05 | Absolute depth rejection threshold. |
| `depthRelativeThreshold` | 0 to 1 | 0.02 | Relative depth rejection threshold. |
| `velocityWeightScale` | 0 to 1 | 0.075 | History confidence reduction with motion. |
| `luminanceWeightScale` | 0 to 16 | 0 | History confidence reduction with luminance changes. |
| `neighborhoodClampExpansion` | 0 to 1 | 0 | Neighborhood clamp expansion. |
| `historyRelaxation` | 0 to 4 | 1 | Further box expansion for history disagreement that alternates in sign. |
| `historyAccumulation` | `effective-samples`, `compatibility-age` | `effective-samples` | History accumulation model; changing it resets history. |
| `historyRectification` | `minmax-clamp`, `variance-clip`, `bounded-variance-clip` | `minmax-clamp` | History rectification method. |
| `varianceClipGamma` | 0.25 to 4 | 1 | Variance clipping bound scale. |
| `historyFilter` | `catmull-rom-clamped`, `bilinear` | `catmull-rom-clamped` | History color reconstruction filter. |
| `currentFilter` | `gaussian`, `point` | `gaussian` | Current-frame reconstruction filter. |
| `motionSelection` | `closest-depth`, `center` | `closest-depth` | Motion-vector selection. |
| `postTemporalView` | `unjittered`, `jittered` | `unjittered` | Raster view for transparent and debug geometry drawn after the resolve. |
| `resolutionPreset` | `native`, `quality` | `native` | Quality renders at 2/3 of the display extent while an active Temporal AA resolve can upscale. |
| `textureLodBiasOffset` | -2 to 1 | -1 | Additional material texture LOD bias while Temporal AA is active. |

Sidecars record the effective settings in `temporal`. Temporal AA overrides
cannot be combined with `-ReferenceSamples`.

### GPU timing

`-GpuTiming` measures every successfully submitted sequence render, including
frame zero, camera cuts and each reference sample. There is no warmup skip.
The sequence status reports, for the whole frame and each scope such as
`PostProcess.TemporalAA`, the sample count and the mean, median, 90th percentile,
minimum and maximum in milliseconds.

After the last sequence render, `finishing` waits for both capture writes and
all pending profiles. Profiling returns to its prior state when timing collection
ends. A missing profile fails the sequence after 64 later successful runtime
submissions; polling, deferred frames and aborted renders do not consume this
budget. Unavailable/disabled profiling or a bound profile with invalid durations
also fails explicitly. Failed or cancelled sequences retain their collected
partial statistics, which must not be treated as a complete run.

For a completed timed sequence,
`gpuTiming.frames` equals `frameCount` multiplied by `max(1, referenceSamples)`;
an individual scope counts only renders that recorded it. Ordinary frames before
or after the sequence, unsubmitted renders and duplicate profiles do not
contribute. These statistics cover the full sequence, independently of the
Temporal AA Lab's interactive timing window.

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File $session sequence -Session atrium `
    -CameraPath SEQ_StaticRailings -CaptureFrames 95 -GpuTiming `
    -TemporalAA 'neighborhoodClampExpansion=1' -Label static-wide-clamp
```

Profiling adds timestamp queries but does not change the rendered image. Time
Release builds and compare configurations within one session and backend.

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
