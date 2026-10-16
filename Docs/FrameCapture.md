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
| Interactive screenshot, graph or resource export in an ordinary/F5 launch | DevTools **Application > Frame Capture** |
| One image of one Lab or Demo | `--capture-on-ready` one-shot launch |
| Several images, views or settings from one process | A session (`GGLabSession.ps1`) |
| Every authored camera view of the content | `GGLabSession.ps1 batch` |
| DX12 and Vulkan agreement | Two sessions, `batch` on each, `CompareCaptures.ps1` |
| Temporal behavior along a fixed camera motion | `GGLabSession.ps1 sequence` |
| Pass order, versions, barriers and actual transient allocations | `GGLabSession.ps1 graph` |
| Exact texture/buffer values before or after a pass | `GGLabSession.ps1 export` |

Sessions and one-shot launches are hidden by default: the window is never shown
or activated, input is ignored and simulation advances with a fixed 1/60 s step.
Use `-Visible` only when the owner allows windows on the desktop.

## Interactive capture and exports

For a normal visible launch, including F5 in Visual Studio or VS Code, open
DevTools **Application > Frame Capture**. No `--session`, PowerShell command or
control connection is needed. Keep DevTools enabled (`--no-devtools` removes the
panel).

- **Capture (F9)** writes the selected Scene or Composited PNG and its JSON
  metadata sidecar. F9 remains a screenshot hotkey; exporting a graph does not
  change its Source setting. When ImGui owns keyboard input, use the button or
  return focus to the rendered view before pressing F9.
- **Capture All Views** queues one screenshot per authored reference view, using
  the selected Source and shared settings. Temporary readback or writer pressure
  keeps later views pending until capacity is available; views remain in order,
  and the next camera change waits for capacity. Each screenshot records the
  frame that actually admits it. The in-flight limits are not a batch-size limit.
- The **Screenshot**, **Render Graph** and **Resources** tabs share capture
  settings. Source appears only in Screenshot; F9 keeps using that source.
- **Export Graph** in Render Graph writes the full compile and execution records
  described [below](#render-graph-export). Enable **Summary only** for executed
  pass order and versioned reads/writes without the execution record.
- **Resources** shows a searchable **Available resources** list from the latest
  immutable RenderGraph diagnostics snapshot. The list can collapse and scrolls
  inside a fixed-height area. Runtime supplies descriptor support using the same
  format rules as actual export. Unsupported resource dimensions, multisampling,
  formats/word layouts, and imported allocations without CopySource are hidden;
  the menu does not grant copy usage to an allocation owned elsewhere.
- Click a resource's **+** to add an independently configurable target. Expand
  each target to choose **Final**, **Before pass** or **After pass**; pass choices
  include declaration occurrences and a culled marker. Before lists readers;
  After lists writers. Texture targets support mip, first slice, all remaining
  slices or an explicit count, and an automatic or explicit color/depth/stencil
  plane. Buffer targets use the full buffer. The same resource can be added more
  than once for different versions or subresources. **X** removes only that target.
- **Export Resources** writes one group of NPY arrays and a JSON sidecar;
  **Include graph** adds the graph from the same captured frame. Target settings
  remain editable after submitting; the queue owns a copy of each address.
  Descriptor support is a discovery hint, independent of contents, pass liveness
  and current capacity: the actual capture frame resolves every address again.
  Missing/culled resources, undefined subresources and unsupported groups produce
  explicit failures; they never retarget a different resource or split frames.
- All export buttons share **Timing**, **Settle Frames**, **Reference View**,
  **Output Directory**, **Label** and **Note** with screenshots. Timing defaults
  to **Next frame**: the next rendered frame, not the already displayed frame.
  Select **After ready** to wait for the readiness gates and settle frames.
  Scene/Composited Source only affects screenshots.
- The pending count and **Current Frame** readiness gates help explain waiting
  requests. **History** retains the latest 16 results, showing the captured frame
  serial and published PNG/JSON paths or a failure. A short notification and the
  application log also name the actual output file, including graph/resource sidecars.
- History groups results in collapsible rows, with the newest expanded. Click a
  filename to copy its full path; hover to inspect it. Resource exports list the
  sidecar, every NPY array and the optional graph. Path text and the clipboard
  use UTF-8; glyph availability follows the configured ImGui font.
- **Open folder** on a successful result opens that result's actual
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

The published schema is [`gglab.render-graph` v1/v2/v3/v4](Schemas/RenderGraph.schema.json)
(v1 without GPU observers, v2 with copies, v3 with boundary scans, v4 with production counters):

- `compile` is sampled before execution. Full mode contains declaration-order
  passes, name occurrences (1-based), culling, execution order, encoder,
  dependencies, accesses with resource versions, barrier intents, acquire/release
  lists, resource descriptors (including mip/slice counts and buffer size/stride),
  resource nodes, dependency edges and compile diagnostics. `duplicatePassNames`
  reports repeated names, including culled declarations. `occurrence` counts equal
  names in declaration order, including culled passes, not in summary execution
  order. Culling therefore cannot renumber a pass; resource addressing uses the same
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
  `occurrence`) for discovery. Resource exports use those addresses;
  the Runtime non-finite scan API and session `watch` reports use the same
  addressing; startup production instrumentation is described below.
- `frame` reuses capture metadata for content, camera, simulation time, render
  settings and readiness; `frame.frame.serial` equals the top-level `frameSerial`.
  It adds the Runtime adapter/driver compatibility identity. Build identity and
  per-pass shader artifact identity are explicitly unavailable with reasons;
  the capture service currently has no authoritative sources for these. The
  parameter revision is null until T4.
- `submission` records the fence handle index/generation and value. A successful
  graph result is published only after that exact fence completes. Aborted,
  failed or missing frames fail exact requests, never substitute another frame.
- `observation` flags the active tools. `observationNodes` is empty for T1 alone: T1 adds no
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

## GPU resource export

`export` writes the stored values of a group of RenderGraph resources from one
frame. It uses protocol v2 and the same readiness, view, timing, output directory,
label, note and `-NoWait` options as `capture`. Discover resource/pass names with
`graph -Summary` first:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File $session export -Session atrium `
    -Targets '[{"resource":"TAA.ResolvedSceneColor"},{"resource":"DisplayView.DepthBuffer"}]' `
    -IncludeGraph -View CAM_Courtyard -Label values
# -TargetsFile reads the same JSON array from a UTF-8 file.
```

An address has `resource`, optional `at` (`"final"`, `{"before":"Pass"}` or
`{"after":"Pass"}`), optional 1-based `occurrence`, `mip` (default 0), and
`slices: [first, count]` (default all slices). Occurrences include culled pass
declarations. `before` requires a read by that pass; `after` requires a write.
`final` selects the last declared version. `aspect` is optionally `color`, `depth`
or `stencil`; depth textures default to their depth plane. Stencil is exported
only when explicitly requested.

The original graph is compiled first. Dedicated copy observers enter at the
selected execution cut, preserving the original culled set and pass order;
lifetimes, usage and barriers are then rebuilt. They can extend transient
lifetimes and change allocation reuse. A culled producer is never revived, and
an undefined mip/slice is never filled in by exporting it. Imported resources
must already support CopySource. Ordinary RenderGraph Read/Export assertions
remain in force.

One group publishes `.npy` files plus `<label>.<frameSerial>.resource-export.json`.
`-IncludeGraph` adds the full graph of that same submitted frame. The completed
response contains `exports`, `metadata`, optional `graph`, and `frameSerial`.
Files publish as one group with the JSON sidecar last; consumers should discover
groups through sidecars. Filename collisions choose one new stem for the whole
group. A failed request reports a reason and, for address/admission failures, a
typed `failureCode` such as `Culled`, `UndefinedContent`, `UnsupportedFormat`,
`UnsupportedResource` or `TooLarge`.

- NPY v1 arrays use little-endian values and C order. Textures have shape
  `[slices, height, width, channels]`; cubes are slices in the existing face order.
  2D textures/arrays, cubes, mips and buffers are supported; MSAA and 3D fail.
- Unorm/sRGB textures retain their bytes and RGBA/BGRA order. Float16/Float32 and
  Uint32 formats retain their bits. D32 is Float32; D24 depth is converted from
  the low 24 bits to Float32 in `[0, 1]`, and stencil is Uint8. Typeless textures
  use a unique typed view of the addressed pass, or flagged raw unsigned words.
- Readback slice starts satisfy both 512-byte placement and texel-block alignment;
  RGB32F uses a 1536-byte alignment. Padding counts against the shared byte budget
  and is removed from the exported array. RenderGraph validates slice placement,
  and Vulkan validates each texture-to-buffer region's offset.
- Registered buffers use a structured dtype with field offsets and stride.
  Initial layouts cover Forward+ tile headers and tile depth ranges. Other
  buffers use `[elements, wordsPerElement]` Uint32 arrays. Unwritten capacity in
  a buffer can contain arbitrary bytes; compare the active records defined by
  the producer's count fields.
- Sidecars record the address, version, writer, physical/view format, dtype,
  shape, layout, extended lifetimes and optional semantics. Scene-color
  semantics include pre-exposure; depth includes its convention; motion is
  current UV minus previous UV, with positive Y down. Unregistered semantics
  are null. Build/shader identities remain explicitly unavailable; parameter
  revision remains null until T4.
- Shared limits are 64 requests, 8 payload slots and 256 MiB including readback
  and tight CPU data. Groups are never split. Standalone groups retry temporary
  pressure with refreshed frame metadata. Scheduled exports wait for the payload
  pool and writer before simulation starts, then check exact sizes after compile.
  A group that cannot fit, including its paired image, fails instead of waiting
  forever. A PNG plus seven targets fills all eight payload slots.

Graph-only frames retain `gglab.render-graph` v1. Frames with copy observers
use v2, with pass `observation` markers, `ObservationOrder` dependency edges and
`execution.observationNodes`. Frames with non-finite scans use v3, whose observer
kinds distinguish `ResourceExport` from `NonFiniteScan` (including clear and
readback passes). Resource sidecars retain v1 unless scans also ran, when they
use v2. PNG metadata v1 retains additive `observation` flags. The existing
`nonFiniteWatch` flag describes scan activity only, not completed boundary or
production coverage. Keep these tools off for performance measurements.

Schedule one target group per sequence frame using `-Exports` or `-ExportsFile`:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File $session sequence -Session atrium `
    -CameraPath SEQ_StaticRailings -CaptureFrames 4,30 `
    -Exports '[{"frame":4,"targets":[{"resource":"DisplayView.SceneColor"}],"graph":true}]' `
    -Label observed
```

The arrays, optional graph and image of frame 4 share its render serial. With
supersampled references, exports occur on the final sample. Duplicate export
groups for one frame and frames outside the path are rejected. Existing capture
and diagnostic sequence options retain their meaning.

Validate sidecars, actual NPY headers/data and paired graph identity with Python
`jsonschema` and `numpy` installed:

```powershell
python Scripts/ValidateResourceExports.py Build/Sessions/atrium-dx12/Captures Build/Sessions/atrium-vulkan/Captures
python Scripts/ValidateRenderGraphExports.py Build/Sessions/atrium-dx12/Captures Build/Sessions/atrium-vulkan/Captures
```

Schemas live in `Docs/Schemas/ResourceExport.schema.json` and
`Docs/Schemas/RenderGraph.schema.json`. Both validators have `--self-test` gates
in CI; GPU value/observation checks still require local hardware runs.

## Non-finite boundary scans and production counters

The Runtime provides explicit boundary scans and opt-in production counters.
Session `watch` supports explicit targets and semantic defaults in report or fail mode.
Capture and sequence coverage completes asynchronously. Boundary scans alone cannot prove
that sanitization or recovery hid no non-finite input.

Start a counter-capable process with `--watch-nonfinite`, or use the session script:

```powershell
& $session start -Session numeric-check -Rhi dx12 -Lab gglab.lab.gtao -WatchNonFinite
& $session status -Session numeric-check
& $session stop -Session numeric-check
```

This startup choice selects Shader-owned counter variants before preloading and
pipeline creation. It does not enable default boundary scans or fail mode. Without
the switch, no counter variant is loaded and no production clear/readback nodes are
added. Vulkan enables pixel storage atomics only when supported; an instrumented
render host rejects devices without that capability.

`status.watch` reports `instrumentation: available | unavailable` and the latest
completed production frame (`Incomplete` until evidence arrives). `sites` lists
nonzero counters or invalid evidence, with program/site identities, replacement
and clamp counts and finite extrema. `submission` records fence/adapter identity.
This is a latest-frame diagnostic, not a sequence acceptance result; it does not
retain a first failing frame or complete a coverage range.

### Session watch reports

Omit targets to watch semantic defaults, or configure explicit addresses with the
same resource/pass/mip/slice syntax as resource exports. Configuration is replaced
as a whole; success increments
`configurationVersion` and resets report history. Invalid requests preserve the
previous configuration. The next compiled frame issues the new boundary scans;
late results of earlier configurations are drained without updating the report.

```powershell
& $session watch -Session numeric-check -WatchClaims boundary
& $session watch -Session numeric-check -Targets '[{"resource":"Temporal.MotionVectors"},{"resource":"GTAO.TemporalAO"}]'
& $session status -Session numeric-check
& $session watch -Session numeric-check -WatchOff
```

The default claims are `boundary` and `production`. `-WatchClaims boundary` works
without startup instrumentation; `-WatchClaims production` needs no target list.
Without instrumentation a requested production claim remains `Unavailable`.
The startup switch enables counter shaders independently of report configuration:
`-WatchOff` stops boundary scan requests and report tracking, while an instrumented
process continues collecting production counters.

`status.watch.report` and the `watch` response contain the configuration version,
requested claims, separate boundary/production frame serials, per-target counts,
finite extrema, scalar index and texture or buffer location. `firstAnomaly` retains
the earliest observed failing frame since configuration, including a production
frame followed by a clean frame in the same completion poll. These are reports of
completed evidence, not proof of complete coverage over a sequence or frame range.
Pending, missing, unsupported or mismatched evidence is `Incomplete`. Capacity
pressure also yields `Incomplete`; continuous report scans yield capacity while
a scheduled resource export waits for the shared payload pool to drain.

The report also keeps since-configuration totals for audits over many frames.
`productionFrames` counts complete production frames; `siteTotals` lists every
program/site that recorded a replacement or clamp, with frame and failed-frame counts,
summed counts, extremes and the first frame; `targetTotals` lists every scanned target
with checked and failed frames, anomaly counts and finite extrema. Reconfiguration
resets them.

Buffers require an explicit owned interpretation and producer-valid element count:

```json
[{"resource":"Values","buffer":{"stride":16,"validElementCount":3,"fields":[
  {"offset":0,"type":"uint32"},
  {"offset":4,"type":"float32","count":2},
  {"offset":12,"type":"float16","count":2}
]}}]
```

Only floating-point fields enter the scan; integers, padding and capacity beyond
`validElementCount` are excluded. The interpretation is checked again against the
actual compiled frame's buffer. Scalar types are `uint8`, `uint32`, `float16` and
`float32`; count defaults to one. A target is limited to 256 fields and 4096 float
components per element. Texture targets omit `buffer`. `-TargetsFile` accepts the
same JSON array from a UTF-8 file.

Default boundary coverage currently implements Tier A: motion vectors when a
temporal consumer requires them, the last floating-point GTAO output before
upsampling, scene color before Temporal AA, and scene color before its earliest
live post-process consumer. The owning pipeline declares applicability and passes
register exact resource versions and observation cuts. Inactive features report
`NotApplicable`; absent, culled, conflicting or mis-resolved active bindings report
`Incomplete` with `MissingCoverage`. A graph with no registry also reports missing
coverage. These classifications never add graph dependencies or revive culled work.

Reports expose `targetsMode`, `defaultTiers` (currently `["A", "B"]`), and each default
target's `semantic`, `watchTier` and `failure`. Unbound targets have a null address.
Resource export `watchTier` is present only for an exact registered default version;
it is not inferred from a resource name.

Tier B covers active IBL environment, irradiance and specular cubes and the scene's
transmittance, multiple-scattering and SkyView atmosphere LUTs. Each mip scans every
array slice/cube face. Registration follows the active resource bank, including a
generation published after cache upload; staging bake resources are excluded.
Atmosphere revisions include writes of the current frame and become evidence only
after a successful submission and completed readback.
The report contains one target per generation mip, so semantic names can repeat.

Generation targets report `generation`, an ephemeral `resourceIdentity` (texture
index and handle generation), and `lastCheckedFrameSerial`. Unchanged, checked
clean content reports `NotApplicable` while retaining its last checked record and extrema;
those values describe that earlier check. Pending or unadmitted mips are `Incomplete`.
Per-frame defaults retain scheduling priority, while generation mips rotate through
the remaining shared payload allowance. In-flight mips are not duplicated. Failed
or cancelled transport remains retryable; superseded results cannot certify a new
generation, but late anomalies still enter `firstAnomaly`. Replacing the watch
configuration rechecks already active content. Failed content remains `Failed` while
that generation is active. Tier C GI defaults remain pending.

`Scripts/TestNonFiniteGenerations.ps1 -Configuration Debug` (or `Release`) verifies
both backends with isolated writable state. It checks fresh-baked and cache-restored
active IBL publication receipts, all mip/face coverage, stable-content deduplication,
watch reconfiguration, and physical atmosphere LUTs.

A `fail` request that needs unavailable production instrumentation returns
`RequiresRestart` without changing the current configuration. Boundary-only fail
mode works without counter variants:

```powershell
& $session watch -Session numeric-check -WatchMode fail
& $session watch -Session numeric-check -WatchMode fail -WatchClaims boundary
```

### Submitted coverage and fail mode

When watch is enabled as a capture is issued, that capture records coverage of its
exact submitted frame. A sequence binds the watch configuration when its first
frame begins and covers every successfully submitted sequence frame, including
uncaptured frames and every reference sample. Aborted render frames are excluded.
Each range retains its own unresolved evidence; it never infers a pass from the
interactive report's latest frame. A generation's later completed mip scans can
fulfil earlier frames that consumed that same physical content revision.

Captures wait for both their payload and the requested watch claims before writing
or publishing files. Sequences remain `finishing` until their watch tail, captures
and requested GPU profiles resolve. A detection after the last sequence submission
still fails fail mode. Changing or disabling the watch configuration invalidates
unfinished ranges as `Incomplete`; it cannot convert their old coverage into a pass.

PNG sidecars, resource/graph export frame metadata, capture responses and sequence
status include a `watch` object (`gglab.nonfinite-watch` v2) when they report a range.
It identifies the bound configuration and mode, requested claims, instrumentation,
first/last submitted serial, frame count, completed result count, separate boundary
and production claims, the first checked anomaly frame and failure reason, and any
Debug injection stamp.
Completed result counts include explicit incomplete outcomes. A requested production
claim without instrumentation completes as `Incomplete`, even though interactive
status labels the capability `Unavailable`. Unrequested claims remain `Unavailable`.
`Scripts/ValidateNonFiniteCoverage.py` validates these nested records.

Fail mode fails affected captures and the sequence with `NonFiniteDetected`,
`NonFiniteReplaced`, `ClampPolicyExceeded` or `MissingCoverage`. Failure text names
the frame and resource/location or program/site. Pending after-ready captures also
fail when the active fail watch detects an anomaly or missing per-frame coverage.
The process keeps running for inspection and watch reconfiguration. Report mode
still writes artifacts with their failed or incomplete claims.

Ranges add no GPU/CPU fence wait. Unresolved frames become `Incomplete` after 64
further successful runtime submissions or 30 seconds without complete evidence;
the time bound also handles a stalled/deferred renderer. Shutdown closes and
adjudicates unfinished ranges after the render host has finalized. These are
liveness limits, not a frames-in-flight assumption. Completed frames reduce into
aggregate claims instead of retaining the whole sequence's records.

Continuous report scans yield to scheduled resource exports and honestly report
the resulting coverage gap. Fail mode attempts per-frame scans alongside exports;
generation work yields priority on those frames and can complete later. Temporary
pressure defers the entire unsubmitted frame, including its capture requests, so
the same sequence pose/sample retries after retirement without a coverage gap.
Unsubmitted `Busy` scan results contribute no failure history. A frame whose image,
exports, scans and counters cannot fit even an idle budget fails explicitly instead
of deferring forever. The shared payload budget remains authoritative.

`Scripts/TestNonFiniteCoverage.ps1` verifies Debug/Release on both backends with and
without counter capability: clean captures, graph/resource exports, full-sequence
coverage, capability rejection, missing-target failure, report-mode incomplete
metadata and continued process operation after failure. CPU coordinator tests also
exercise delayed anomalies, out-of-order results, missing readbacks, configuration
changes, generation tails and reference-sample membership. Full-scene
policy/overhead acceptance remains separate work.

### Debug non-finite injection

Debug builds accept one injection request in `watch`, so tests can prove that watch
catches what sanitization and recovery would hide. Injection points are registered in
`Shaders/Metadata/NonFiniteSites.json` (`injectionPoints`) and checked like sites.
Each point replaces one component of its operand at one location; the request names
the point, a value and the location:

| Point | Claim | Location | Component |
| --- | --- | --- | --- |
| `pass-temporal-aa.csmain.history` | production | display pixel | history RGB (0-2) or accumulation (3) |
| `pass-render-graph-compute-smoke.psmain.preview` | production | display pixel | RGBA output (0-3), then `SanitizeHDRChannel` |
| `pass-render-graph-compute-smoke.csnonfiniteprobe.probe` | boundary | probe texel | RGBA32F `RenderGraphCompute.NonFiniteProbe` |

Values are `NaN`, `PositiveInfinity`, `NegativeInfinity`, `Overflow` (131072, finite
above the FP16 maximum) and `Negative` (-4). The request needs the production claim and
a process started with `--watch-nonfinite`; otherwise `watch` returns `RequiresRestart`
or rejects it. Release builds reject `inject`.

```powershell
& $session start -Session inject -Configuration Debug -Lab gglab.lab.temporal_aa -WatchNonFinite
& $session watch -Session inject -WatchMode fail -WatchClaims production `
    -Inject '{"point":"pass-temporal-aa.csmain.history","value":"NaN","x":161,"y":93,"component":1}'
```

The counter clear pass writes the frame's request after the site records, and each
point reads it from the counter buffer. Only counter variants contain injection code,
so ordinary pipelines, settings and root constants are unchanged. The readback returns
the request and an applied count with the counters: evidence that does not carry the
configured request is `Incomplete`. Reports (`watch.report.injection`, with the latest
`appliedCount`), the latest production frame (`watch.injection`) and coverage metadata
(`injection` with `injectedFrames` and `appliedFrames`) are stamped, and fail-mode
failure text ends with `under Debug injection '<point>'`.

The probe pass of `gglab.lab.render_graph_compute` exists only for frames whose
injection targets the probe point; the preview declares a read so the probe stays live
for boundary scans. The preview point adds the sanitized value to the written pixel,
which therefore stays unchanged for non-finite and negative values. That site's
negative clamps are `expected` and its FP16-maximum clamps are `error`. This Lab
renders no IBL, so its `ibl` readiness gate never opens; use `-Timing next-frame`.

Debug launches also accept repeatable `--lab-parameter <id>=<value>` startup
assignments (`GGLabSession.ps1 start -LabParameters`), parsed strictly against the
startup Lab's declarations before its first preparation. Release builds reject them.

`Scripts/TestNonFiniteInjection.ps1` verifies Debug sessions on both backends with one
session per Lab: exact probe locations and counts for NaN and both infinities, a finite
overflow that only changes the maximum, a sanitized NaN that fails production while the
PNG pixels equal the uninjected control, `expected` and `error` clamp policies, and
Temporal AA history NaN/infinities that fail only at the color or accumulation recovery
site. Every injected report and capture must carry the stamp.

### Lab and Demo audit

`Scripts/AuditNonFiniteLabs.ps1` starts one hidden, counter-capable session per Lab or
Demo and backend (Release by default), configures report-mode watch with semantic
defaults and production before the first frame settles, waits for readiness, then plays
every registered camera path as a sequence or observes content without paths. It
writes `result.json` and `summary.md` (runs, per-site totals with the reviewed clamp
policy, boundary targets with anomalies) under `Build/Validation/NonFiniteAudit`.
`-Content` selects names, such as `gtao,coastal-atrium`. The audit gathers evidence
for policy review; it does not change policies.

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/AuditNonFiniteLabs.ps1 -Rhi dx12 -Content gtao
```

`gglab.lab.render_graph_compute` and `gglab.lab.coordinate_conformance` run custom
pipelines without the IBL bake, so their `ibl` gate never opens; the audit observes
them for `-ObserveSeconds` instead.

### Runtime counter and scan mechanics

Production counter details:

- The site inventory routes sanitization call-site IDs through the color helpers
  to their actual scalar clamp. Recovery guards observe original operands without
  changing their value. Geometric validity-only sites remain report-only.
- Counter variants use a reserved `u31, space0` buffer in physical pipeline layouts.
  Their durable variant identity includes the reviewed inventory's SHA-256. A
  generated shared table keeps CPU/HLSL indices synchronized; a frame-tagged header
  rejects stale, mismatched or partial records. Wrapped counters are `Incomplete`.
- After the original graph's liveness is fixed, one clear and one readback bracket
  the live passes. Each original shader pass declares its counter read/write;
  ordinary dependency, lifetime and barrier planning owns synchronization.
  Instrumentation never revives a culled producer or writes a rendered resource.
- Counter readbacks and retained immutable CPU results share capture's payload
  slots and byte budget. Completion polls the submission fence; normal operation
  introduces no CPU wait. Counter helpers issue atomics only for anomalous values.
- Non-finite recovery/replacement always fails. Finite clamps use the reviewed
  site's `report`, `expected` or `error` policy. Budget evaluation exists in Runtime,
  but budgeted inventory entries still require explicit reviewed limits.

Graph frames containing production counters use schema v4 and mark clear/readback
observers as `ProductionCounters`; original pass accesses expose their counter
buffer dependency. Other graph schema versions retain their meaning.

Implemented Runtime integration:

- `FrameCaptureControlBase::RequestNonFiniteScan` accepts explicit resource
  addresses, an owner-assigned configuration version, optional exact frame
  serial and optional paired graph. `PrepareResourceExports` prepares resource-export copies
  and boundary scans together, once, after ordinary graph compilation. Resolution
  preserves the original pass occurrences, culling and version cuts; the ordinary
  `Read`/`Export` assertions remain unchanged.
- Each target gets graph-owned clear, scan and readback passes. The general
  lifetime/barrier planner owns the transient record, clear-to-scan UAV ordering,
  source reads and scan-to-copy transition. Texture SRVs select one mip and the
  addressed array/cube-face range. Float16/Float32 R/RG/RGBA, RGB32F and D32
  interpretations are accepted subject to backend SRV support; integer, normalized,
  stencil and ambiguous typeless views fail explicitly.
- Buffers require an owned `NonFiniteBufferScanLayout`, built from a registered
  or explicit `GpuBufferLayout` and a producer-valid element count. Admission
  revalidates it against this frame's descriptor; integers, padding and inactive
  capacity are excluded. Allocated capacity is never a substitute for valid count.
  Imported buffers need shader-readable `Structured` usage; imported textures
  need `Sampled` usage. Resource export independently still requires `CopySource`.
- Scan programs resolve lazily through the Shader program catalog and Runtime
  pipeline resolver. Unavailable programs fail the request before graph mutation;
  no host-side compiler is added to Runtime.
- Scans share capture's request, payload-slot and byte budgets with PNG/resource exports.
  Reservations include GPU record/readback, peak CPU record copies, component
  upload and owned CPU component tables. Readbacks and uploads survive submission
  until the common frame fence completes. Final CPU record/table consumers keep
  their payload slots and bytes until the last owner releases them. Normal polling
  adds no CPU wait. Busy groups are not split; abort, retry, failed submission,
  allocation and mapping errors use the existing capture lifecycle.
- `FrameCaptureResult::m_NonFiniteScans` carries immutable records, addresses,
  versions, buffer interpretation and evaluated boundary claims; the enclosing
  result carries the frame serial and submission fence/adapter identity.
  `Completed` describes transport, not a passing claim. Missing/partial/malformed
  records evaluate to `Incomplete`; detections evaluate to `Failed`. The production
  claim is explicitly `Unavailable`. Earliest index means the minimum checked
  scalar index, not the origin of production. Texture order is `[slice,y,x,channel]`;
  buffer order is `[valid element, floating-point component in byte-offset order]`.

The Shader-domain foundation also includes the 32-byte CPU/GPU ABI, IEEE-bit
classification, wave reductions, finite extrema and clamp-policy evaluation.
`Shaders/Metadata/NonFiniteSites.json` inventories 76 sanitization/recovery/validity
sites and 3 Debug injection points. Its checker is a conservative lexical review
gate, not data-flow proof; counter and forwarded-counter routing are source evidence,
not GPU verification.

Developer checks (after building the named test projects):

```powershell
python Scripts/GenerateNonFiniteSites.py --check
python Scripts/ValidateNonFiniteSites.py --self-test
python Scripts/ValidateNonFiniteSites.py
powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/TestTemporalAACounters.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/TestNonFiniteInjection.ps1
Build/Output/x64/Debug/GGLabRuntimeTests.exe --suite nonfinite-watch
Build/Output/x64/Debug/GGLabRuntimeTests.exe --suite frame-capture
Build/Output/x64/Debug/GGLabAppRuntimeTests.exe
Build/Output/x64/Debug/GGLabShaderRuntimeIntegrationTests.exe --suite nonfinite-scan-gpu --rhi dx12
Build/Output/x64/Debug/GGLabShaderRuntimeIntegrationTests.exe --suite nonfinite-scan-gpu --rhi vulkan
```

`TestTemporalAACounters.ps1` runs the production TAA Lab in hidden Debug/Release
DX12/Vulkan sessions at 320x180 by default; `-Configuration`, `-Rhi` and
`-WindowSize` can narrow or override that matrix. Native and Quality replays
capture cold and accumulated history with counters off/on, requiring identical
PNG pixels and same-frame pre-TAA/resolved-color NPY bytes within each backend.
Counter-on transitions also exercise depth-only opaque and alpha-tested draws
with TAA and temporal GTAO disabled, then restore TAA. It checks watch reports,
native error logs and process exits. These reports
retain latest completed evidence and the first observed anomaly, not complete
sequence-range coverage. Results go to a unique run directory under
`Build/Validation/TemporalAACounters`, or under `-OutputDirectory`.
Each case separates `sessionChecksPassed` from `equivalencePassed`; any off/on
pixel or array-byte difference still makes the overall check exit 1, even when
the watch and lifecycle checks pass. Inspect `images.json`, `arrayDifferences`
and the per-arm receipts instead of treating clean boundaries as equivalence.

The hidden-window hardware suite covers buffer clear/scan plus the real capture
service and RenderGraph path. RGBA32F/RG16F cube-compatible textures exercise
nonzero mip and face ranges, NaN/positive and negative infinity, finite extrema
and first index. Resource exports of every fixture resource are byte-identical with
scans off/on. This establishes fixture observation equivalence, not whole-Lab
visual equivalence or overhead. The hardware suite also renders a sanitizing
pixel shader and a recovery compute shader with counters off/on, compares their
exported image and values bit-for-bit, and checks exact per-site replacement/clamp
counts after the common frame fence. Whole-Lab acceptance and measured overhead
remain separate checks.

Local verification on 2026-10-15 passed 56 GPU checks per backend in both Debug
and Release, including bindless layout slots before the counter binding. Runtime
frame-capture passed 199 checks and scan/policy foundations passed 45. The GTAO
Lab also completed hidden Release captures and schema-valid graph exports on
both backends, with production status `Passed`. These smoke runs do not establish
acceptance across every Lab or a complete watch coverage range.

The explicit session report layer passed 27 coordinator checks and 30 control
checks in both configurations. Hidden Debug GTAO sessions on both backends
reported clean motion/AO boundaries and production evidence, completed PNG and
paired resource/graph exports while reporting, and classified an absent target
as `Incomplete`. A Release session without counter capability reported clean
boundaries with production `Unavailable`, preserved configuration on rejected
requests, and exported an observer-free graph after disabling watch. Resource
and graph validators accepted all exported groups, including graph v4 pairs.

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
and encoder working memory. A single screenshot without capacity fails before
GPU submission. Capture All Views waits for temporary pressure before applying
the next view and issuing its capture; sequences wait before advancing their
requested frame. Unsupported or oversized images fail explicitly. Once a
capture is issued, its frame is fixed even if the request waited for capacity.

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

Directory comparison enumerates PNGs and reads each matching JSON sidecar;
session receipts and resource/graph exports in the same directory are ignored.
A missing or malformed screenshot sidecar, an unsupported `schemaVersion`, or
an `image.file` that does not name the paired PNG is listed under `rejected`
and makes the exit code 1. `Scripts/TestCompareCaptures.ps1` checks this discovery
and rejection behavior without a GPU, including exact pixel differences.
Each pair lists the frame settings that differ between its sidecars under
`metadataDifferences`: camera,
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
