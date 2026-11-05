# Verification

Use this directory for repeatable regressions and controlled experiments.
[RunVerification.ps1](RunVerification.ps1) owns the suite catalog; `-List` reports
its current groups and supported configurations. Script parameters and validators
remain authoritative for each check.

`Tests/Verification` contains regression entry points and domain-specific oracles.
[Scripts/Verification](../../Scripts/Verification) contains the session client,
shared support and product validators. Native C++ tests remain with their owning
targets: use the [Atlas cards](../../Docs/Architecture/Module_Catalog.md) to find
those tests and the [CI workflow](../../.github/workflows/build.yml) for executable
build/test commands.

## Prerequisites

- Run commands below from the repository root in PowerShell on Windows with
  repository script execution enabled.
- Select a Python environment with `jsonschema`, `numpy` and `Pillow`. The
  contracts group includes the GI Viewer validator, which imports Pillow even
  for its CPU self-test. Pass the Python executable through `-Python`.
- GPU checks require the selected application build and its shader compiler/DXC
  deployment. Follow [AGENTS.md](../../AGENTS.md#build-and-verification) for the
  MSVC toolchain and build settings. Stop running applications before linking.
- Use Develop for routine GPU correctness checks, Debug for additional checks,
  and Release for measurements. Exercise both RHIs for shared rendering changes.

Examples use `$python` for the configured interpreter:

```powershell
$python = 'python' # Or the absolute path to the environment's python.exe.
```

## List and run suites

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File Tests/Verification/RunVerification.ps1 -List
powershell -NoProfile -ExecutionPolicy Bypass -File Tests/Verification/RunVerification.ps1 `
    -Group contracts -Python $python
powershell -NoProfile -ExecutionPolicy Bypass -File Tests/Verification/RunVerification.ps1 `
    -Suite session-parameters -Configuration Develop -Rhi dx12,vulkan -Python $python
```

The default group is GPU-free `contracts`. GPU suites require existing builds and
otherwise default to Develop on both APIs. Select focused suites rather than
running every GPU regression. The catalog runner accepts comma-separated suite,
configuration and RHI lists; unsupported configurations reject before launch.

Standalone scripts have their own parameters. When invoking them within
PowerShell, pass actual arrays such as `-Rhi @('dx12', 'vulkan')`; do not assume
that every script splits comma-separated strings passed through `powershell -File`.

## Repository checks

These read-only checks cover map navigation, source ownership and generated
project metadata:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CheckArchitectureAtlas.ps1 `
    -RootDir . -BaseRef origin/main
powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CheckArchitectureAtlas.ps1 -SelfTest
powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/ValidateProjectBoundaries.ps1 -RootDir .
powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/GenerateProjectFilters.ps1 -RootDir . -Check
```

The Atlas helper reports links/anchors, baseline ancestry, external dependencies
and affected entries. Exit zero means it produced a report: inspect navigation
and publication findings plus incomplete impact coverage. `-AsJson` exposes the
same report for tools. The boundary validator separately checks project ownership
and include rules, including unique feature-domain membership for shader sources,
shared headers and metadata. After shader additions or ownership changes, run
`Scripts/SyncShadersToVS.ps1 -RootDir .` to regenerate project entries and filters.
The GPU-free `shader-project-ownership` suite checks routing, missing/duplicate/wrong
owners, item types and conditions, source-byte preservation and no-op synchronization:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File Tests/Verification/RunVerification.ps1 `
    -Suite shader-project-ownership
```

Map maintenance belongs to the
[Atlas guide](../../Docs/Architecture/Boundary_And_Drift_Guide.md).

## GPU regressions

GPU scripts normally create hidden, task-owned sessions and isolated state/output
directories. Keep the script and executable at the same revision. Check both the
regression verdict and native logs for assertions, shader failures, validation
errors and lifetime/synchronization problems. Finish owned sessions before a
rebuild; do not stop unrelated developer sessions.

For ad hoc captures, parameter experiments, sequences and graph/resource exports,
use the [frame-capture workflow](../../.agents/skills/gglab-frame-capture/SKILL.md).
It owns session commands, waiting and product consumption; this README covers
repeatable test entry points.

### TAA comparison

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File Tests/Verification/RunVerification.ps1 `
    -Suite taa-comparison -Configuration Develop -Rhi dx12,vulkan -Python $python
```

[Invoke-TaaComparison.ps1](../../Scripts/Verification/Invoke-TaaComparison.ps1)
pairs same-frame images and full graph exports for Active/Disabled arms, checks
`PostProcess.TemporalAA` against executed pass indices rather than merely finding
a pass in the graph, and restores the original Host override state. Culled passes
remain present in full exports. Read `result.json`, `journal.json` and
`comparison.json`; restoration, submitted effects and rendering verification are
separate claims. Pixel metrics do not determine TAA image-quality acceptance.

See the [guarded comparison guide](../../.agents/skills/gglab-frame-capture/references/captures.md#guarded-taa-comparison)
for fixture requirements, attached-session ownership and recovery procedures.

### GI receiver preparation cache

`gi-receiver-cache` checks complete diffuse/visibility arrays, partition invariance,
repeated resolve-only frames and unchanged persistent sums/compensation/RNG. Its
small receiver query budget forces multi-submission preparation on both backends.
The Runtime `ray-query-contracts` suite owns preparation settlement, pending fences,
replacement, failure and unknown-completion tests.

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File Tests/Verification/RunVerification.ps1 `
    -Suite gi-receiver-cache -Configuration Develop -Rhi dx12,vulkan -Python $python `
    -OutputDirectory Build/RCV
# Optional same-backend, byte-exact comparison with controls collected before a change:
& ./Tests/Verification/TestGIReceiverCache.ps1 -Configuration Develop `
    -RunDirectory Build/ReceiverCache/Compared -BaselineDirectory <control-directory> -Python $python
# Native Coastal coverage, including primary envelope/interpolant matches:
& ./Tests/Verification/TestGIReceiverCache.ps1 -Configuration Develop -Coastal `
    -ReceiverQueryWork 16777216 -RunDirectory Build/ReceiverCache/Coastal -Python $python
```

`-RequireCache:$false -ReceiverQueryWork 0` collects controls from an older binary
without the cache parameter. Raw logs are retained; a known GameInput-unavailable
startup diagnostic is reported separately from GPU/runtime validation errors.
Source/compiler/product binary seals and artifact audits accompany each current run.
These are correctness observations, not quiet performance or convergence certificates.

### Probe update

Run the CPU oracle contracts and the numerical/lifecycle experiment:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File Tests/Verification/RunVerification.ps1 `
    -Suite gi-probe-update-contracts -Python $python
& ./Tests/Verification/TestGIProbeUpdate.ps1 `
    -Configuration Develop -Rhi @('dx12', 'vulkan') -Python $python
# The same full GPU experiment through the catalog runner:
powershell -NoProfile -ExecutionPolicy Bypass -File Tests/Verification/RunVerification.ps1 `
    -Suite gi-probe-update -Configuration Develop -Rhi dx12,vulkan -Python $python
```

Both GPU entry points return one JSON control receipt with `ok` and the run
directory. The standalone producer retains its `status`-based `result.json` and
reports per-case progress through `-Verbose`; the catalog records the producer
receipt and exit code. A numerical or artifact-audit failure fails either entry.

[TestGIProbeUpdate.ps1](TestGIProbeUpdate.ps1) captures ray/probe arrays and graphs,
checks integration, schedules/history, inputs and resource lifetimes, and records
allocation/activation diagnostics. It runs with standard native validation;
these captures are not quiet cost samples. Inspect the root `result.json`,
`validation.json`, oracle/audit receipts and per-backend `output.log`.
`-Smoke` reduces the matrix; `-SkipCoastal` also omits Coastal lifecycle coverage.

For cost acquisition, use [standalone measurements](#standalone-measurements).
For other GI fixtures, frozen inputs and reference/visibility checks, start with
[GI qualification inputs](GlobalIllumination/README.md).

### Static field update

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File Tests/Verification/RunVerification.ps1 `
    -Suite gi-field -Configuration Develop -Rhi dx12,vulkan -Python $python
```

[TestGIField.ps1](TestGIField.ps1) verifies field updates and lifecycle on the probe
fixtures through `gi_reference.field_enabled`. Field mode uses full updates, Shared
blending and rotating directions. Runtime `ray-query-contracts` covers field
descriptors and CPU settlement.

### Selected-probe visualization

`RunVerification.ps1 -Suite gi-probe-selection -Rhi dx12,vulkan -Python $python`
runs [TestGIProbeSelection.ps1](TestGIProbeSelection.ps1). It checks generation-bound
selection, ordinary-budget isolation and independent numeric inspection. Inspect
`validation.json`, artifact audits and native logs.

### Field readiness

`RunVerification.ps1 -Suite gi-field-readiness` runs F1 plane, F2, F4, an excluded-block
control and F10 black field on both RHIs. It compares the GPU's
Active-probe median floor, four-update change records and small-fixture atlas
deltas with an independent FP64 oracle. After-ready jobs wait for the Runtime
`gi-field` gate; Unconverged fails the waiting job. Explicit next-frame diagnostics
retain timed-out measurements. Engineering success and the 120-update readiness
quality gate are reported separately in `validation.json`.

`readiness.giField` schema v1 freezes field/scene/lighting/environment identities,
the completed epoch-relative update and Ready update, block counts, floor,
threshold, noise input and effective hysteresis. Readiness means observed
stationarity, not reference convergence or a temporal-RMS certificate. The default
zero noise input retains the experimental 1% threshold; it is not a noise
calibration. The readiness suite first records separate frozen-input refresh/steady
per-probe RMS change estimates (up to 64 measurements after warm-up), preserves
the uncalibrated gate result, then explicitly adopts both measured inputs through
`field_refresh_noise` / `field_steady_noise` and reruns with another seed. These
bounded noise estimates are not reference or temporal-quality certificates.
`gi_reference.field_readiness=false` explicitly selects the previous
fixed-settling diagnostic path, without this gate or refresh policy. The existing
field/sampling/injection suites select that path to preserve their estimator.

### Static field sampling

`RunVerification.ps1 -Suite gi-field-sampling` runs hidden DX12/Vulkan receiver
lookup and Forward composition checks through `TestGIFieldSampling.ps1`.
The paired eight-resource group includes both atlases, classification, raster
position/normals and query outputs. `ValidateGIFieldSampling.py` independently
evaluates the interpolation in FP64, checks support/coverage and graph export
order, and verifies update-only versus disabled image equality. Separate images
inspect diffuse composition; they are not the resource group's submitted frame.

The suite uses Native 320x200 and fixed update settling, not GI readiness.
An A0 comparison uses paired HDR/field/receiver/IBL data and GTAO visibility to
check that the volume-edge blend attenuates only the IBL share. The unsupported
cage is a separate classification/query contract, not frozen F10 qualification.
F4 includes floor, separate red/green-adjacent submasks and back-wall measurements
against matched D=1 receivers and an independent quadrature oracle. Its precision
profile settles for 3,072 submitted frames; this is not a Ready/stability certificate.
Use `TestGIFieldSampling.ps1 -CaseId sunroom,sunroom-back` for a scoped rerun;
A0 controls still run, with environment publication completed before field creation.
Its F1/F2/F4 quality measurements preserve passing and failing thresholds in
`validation.json`; engineering lookup success does not grant static-field quality,
reference convergence, target-extent acceptance or complete GI performance.
The field coverage contract is a separate fixture, not frozen F8 qualification.

### Single-bounce field baseline

`RunVerification.ps1 -Suite gi-field-baseline -Configuration Develop -Rhi dx12,vulkan
-Python $python` runs [TestGIFieldBaseline.ps1](TestGIFieldBaseline.ps1). It keeps
transport unchanged and measures the existing feedback-off field on F1 plane,
F2 interior, F4 floor/back and F10 at Native 320x200. `-CaseId` on the standalone
script selects a smaller scope; it does not qualify omitted fixtures or adapters.

Independent seeds at N and 2N must agree within 1% per frozen ROI for total, Sun
and environment E; F4 additionally checks independent quadrature. D=1 sampling
qualification is not depth convergence or per-pixel reference qualification.
Bounded sample doubling preserves unqualified observations instead of increasing
thresholds. The runner separately retains the zero-noise readiness job, measures
refresh/steady change noise and explicitly adopts it with a different field seed.

After Ready plus at least 120 completed updates, each `GI_Static_<reference-view>`
path exports 120 consecutive receiver E/query/position and GPU update records.
The validator rejects gaps, view/input/field-epoch changes, incomplete receivers
and stale readiness. Energy/chromaticity and temporal RMS are separate gates;
stable bias cannot pass accuracy, and accurate mean energy cannot hide flicker.
Read `validation.json` even when a gate fails. A failed selected baseline exits
nonzero while retaining its measurements, artifact audits and identity seals.
No renderer threshold, profile, GI default, whole-G3 acceptance or quiet timing
qualification is changed. CPU/adversarial checks use `gi-field-baseline-contracts`.

For settling diagnosis, the standalone runner accepts `-DiagnosticReadyOffsets
512,1024,2048` (at most four increasing offsets in [512,8192]). Each additional
120-update window waits for the original field's fence-completed Ready update
plus that offset, then records its actual GPU update range and ROI luminance
series. It must retain the same epoch, Ready stamp, camera and inputs. These
windows appear as `diagnosticOnly` in `validation.json`: a later accurate window
never replaces or grants the primary Ready-plus-120 qualification. F4 reference
sampling can be bounded separately with `-InitialF4Samples` / `-MaximumSamples`.

Readiness-enabled fields normalize their finite integration mass when switching
from refresh to steady decay. The nominal profile decays are unchanged; the
readiness snapshot reports the actual effective hysteresis for its completed
update. Irradiance lighting refresh retains its rapid response without restarting
distance history. Readiness-disabled fixed-settling controls keep their legacy
EMA. Noise inputs must be measured for the current integration schedule rather
than copied from a prior implementation; none of them certifies reference accuracy.

`gi-field-feedback` checks the opt-in `gi_reference.field_feedback` transport.
The Lab control safely restarts the session and field; the Runtime creation flag
is immutable. Trace reads the previous irradiance/distance/classification through
declared SRVs, and Blend publishes sticky numerical failure before consumers.
`GI.Field.Feedback` exports v1 pairs of uint4 records per probe: support counts
(Valid, LowConfidence, NoValidSupport, OutsideCoverage), then initializing,
incompatible, consumed age and one-based update. Completed CPU counters are in
`GIFieldDiagnostics::m_Feedback`. The first update contributes zero higher-bounce
energy; later full updates consume age one, including before Ready.

The standalone `TestGIFieldFeedback.ps1` verifies safe toggles, support accounting,
same-update readbacks, Trace-before-Blend graph versions, and an independent FP64
previous-atlas hit-lighting oracle on F1. Off/disabled controls explicitly wait
for 512 additional fence-completed updates in the same field generation and
validate the exported GPU age against the settling receipt; `next-frame` exports
alone do not establish settling. The runner records 120 consecutive Ready-plus-120
observations and separate export-free GPU timing ranges on/off. Timing is scoped
to the recorded configuration/extent, not production performance acceptance.
Without `-ReferenceDirectory`, F4 feedback-on accuracy remains explicitly
unqualified. Supply a matching `TestGIReferenceConvergence.ps1` run collected with
`-FixtureExtent 320x200` and qualified by both seed/sample and depth doubling;
the original fixture default remains 640x400. D=1 cannot certify feedback-on
energy. `gi-field-feedback-contracts` runs the CPU/adversarial checks.

The baseline and feedback runners accept `-ShaderCacheDirectory` with matching
`dx12`/`vulkan` shader cache/artifact directories. Each run copies only those
artifacts into its own state; scene/session settings remain isolated. This can
avoid the host's bounded cold shader compilation startup timeout.

### Static-geometry published-lighting response experiment

`gi-field-light-step` runs [TestGIFieldLightStep.ps1](TestGIFieldLightStep.ps1).
It explicitly enables the default-off `gi_reference.lighting_step_experiment`
in the F4 geometry to select Physical Sky, not the frozen constant-sky F7 fixture.
Without this flag F4 keeps its original lighting, including its existing handling
of the generic environment parameter. `gi_reference.sun_step_lux` is a Deferred lighting binding available
only in this experiment; the existing restart-class `sun_lux` is unchanged.
`GI_LightStep_F4_Floor` retains the authored view/exposure for 360 submitted frames,
with the selected illuminance event at frame 30. The default `-Step increase`
remains 10000-to-20000 lux. The version-1
[recipe catalog](GIFieldLightStepRecipes.json) also declares `decrease`
(20000-to-10000), `small-increase` (10000-to-12000), and `small-decrease`
(12000-to-10000). Pass comma-separated `-Step` IDs to collect multiple recipes.
Only the experimental Deferred control gains 12000 lux; ordinary `sun_lux`,
fixture IDs and production profiles do not change. The starting illumination is
consumed before constructing each calibration field. No field/session rebuild or temporal
reset is permitted at the event. After initial publication, pending IBL requests
must not pause GI: updates keep using the built frame's active bank until the
new generation publishes. Feedback off/on and unjittered GI-only/Native
TAA Final are separate replays. The standalone `-Tap` and `-Feedback` options
select a smaller evidence scope; shader seed directories work as above.

`sequenceGIFieldRefresh` advertises the narrow readiness binding: only the
sequence's actual published/consumed lighting receipt, the current requested/built
generation and an initialized unchanged field/scene can cover that field's refresh.
Its epoch must remain fixed through the estimator's existing refresh-to-steady
quiet-window handoff. That handoff requires an observed refresh-mode Ready and
matching completed steady-mode measurement identity; coverage closes at steady
Ready. Failed gates, unrelated changes and readiness loss after steady Ready still
fail. A last frame that remains Pending or only refresh-mode Ready cannot complete
the sequence. No estimator, hysteresis or threshold is changed; the first
refresh-mode Ready and its own-endpoint error remain separately observable. Ordinary after-ready captures
still require every gate; `expectEffects` grants no exception.

Every frame exports its numerical tap and raw per-probe irradiance/change/update
records. Final is scene-linear TAA output divided by recorded pre-exposure; its
interior masks are replayed independently from the same unjittered authored view.
The validator joins the event's `firstConsumed` to exact submitted-frame metadata,
reports `t_enter`, `t_confirm` (30-frame hold), overshoot, endpoint RMS and the
recipe's own endpoint error when the field first reports Ready. The 45/60-frame
and 10% overshoot targets are reported, not redefined. Missing response is never
counted as a timing pass. `gi-field-light-step-contracts` checks attribution and
response arithmetic adversarially. The headless AppRuntime executable accepts
`--suite <suite-id>` (or `all`); `gi-field-lighting-refresh` isolates the receipt,
phase-handoff and rejection regressions, while `gpu-fault-lifecycle` includes the
build-time publication/aborted-frame regression.

For small-signal engineering attribution, use `-TransientAttribution -Tap gi
-Feedback off -Step small-increase,small-decrease`. Optional `-TraceSeed
61052,71052,81052` and `-PreEventSettleFrames 128,145` expand the Cartesian replay
matrix without changing the default qualification recipe. Calibration still uses
independent seed 61051; trace seeds must differ. Settling requests are minimum
pre-event waits, not exact update-phase locks: the analyzer reports the actual
one-based update at `firstConsumed`. This mode cannot request references or Final.

The opt-in mode observes frames 29–80 and 359 with eight paired payloads:
receiver irradiance/readiness, the pre-blend and final irradiance atlases, final
distance/classification, rays, and receiver query. Atlas slices are planned from
all required receivers' eight-corner cages; a separate paired geometry/normal
capture supplies the immutable authored view. Only frame 0 requests an image,
avoiding the eight-slot capture budget. It does not change GPU algorithms or
production diagnostics. Export-free performance is not measured.

[AnalyzeGIFieldTransient.py](AnalyzeGIFieldTransient.py) also accepts a historical
`--root` and a **fresh** `--output` directory. It re-reads the raw observations,
retains every required ROI and own-target failure, writes `firstConsumed`-aligned
CSV curves with both step/endpoint normalization, and seals its numerical inputs
and exact analysis-code snapshot. Historical two-target captures supply only
population-level probe observations, not receiver contributions. Rich captures
add FP64 receiver closure, exact symmetric atlas/normalized-weight decomposition
against frame 359, static classification checks, and FP64 Trace/Blend checks for
the 16 highest-influence probes during the pre-Ready refresh interval. The single
settled decomposition frame is not substituted for the 30-frame timing endpoint.
The decomposition attributes exported signal changes; it does not by itself prove
an estimator root cause. Trace arithmetic retains its 0.5% component budget and
0.001 normalization floor: mismatches are aggregated into the saved report, keep
`contractsPassed: false`, and return a nonzero analyzer exit. Neither complete
collection nor near-black conditioning turns a failed arithmetic check into a
pass. These checks join `gi-field-light-step-contracts`.

A complete engineering collection returns `engineering-complete` even if response
targets fail, retaining `observedResponseTargetsPassed: false` separately.
Engineering completion is never independent-reference response quality, F7, G3 or
performance qualification; transport/arithmetic failures still fail the runner.

By default this is an experiment/transport check, not response-quality
qualification. Add `-QualifyReferences` to collect independent endpoints for all
selected recipes before the replays. Endpoints are shared across directions only
within the same sealed source/binary/device/input cohort. The experiment reference
schema is version 2 for the additional 12000-lux endpoint; version 1 remains readable
only for its original 10000/20000-lux endpoints. The ordinary reference schema is
unchanged. This reuses the convergence runner's explicit
`-LightingStepLux <10000|12000|20000> -FixtureExtent 320x200` mode, with
`-SingleBounce` for feedback off; feedback on requires sample- and depth-converged
multibounce transport. It does not reuse or reinterpret constant-sky F4/F7
certificates. All four required ROIs, RGB/luminance and total/sun/environment
components retain the independent-seed, N/2N, <1% reference gates and 1e-6 floor.
D=1 qualifies sampling only, never depth convergence. Default acquisition caps
are 262144 samples, depth 32 and 3600 seconds per endpoint/backend; the
`-ReferenceInitialSamples`, `-ReferenceMaximumSamples`, `-ReferenceMaximumDepth`
and `-ReferenceMaximumSeconds` options can bound the run further.

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File Tests/Verification/TestGIFieldLightStep.ps1 `
    -Configuration Develop -Rhi dx12,vulkan -Tap gi,final -Feedback off,on `
    -QualifyReferences -ShaderCacheDirectory Build/Validation/GIFieldFeedback/Seeds `
    -Python <numpy-python> -RunDirectory <new-run-directory>
```

Add `-Step decrease,small-increase,small-decrease` for the downward and small-signal
matrix (24 replays with both RHIs, feedback modes and taps); include `increase` to
collect all four recipes in one 32-replay cohort.

Reference records must match source, binaries, backend/device, effective lighting,
authored view and exact receiver masks. The reference fingerprint uses ordinal
UTF-16 path order regardless of the shared seal's enumeration order; changed,
missing, additional or duplicate source paths are not waived. Every sample in a
GI-only settled endpoint window must be steady Ready plus 120 updates and retain the 10% energy, 2% chromaticity and 2% temporal RMS
gates. Every declared required ROI must have the reference sign and at least half
its independent amplitude. Response timing/bands still use the realtime recipe's
own endpoints. Final is never compared to irradiance or forced to share its sign.
The original transport runner also fails if its own response targets fail.

`validation.json` separates `contractsPassed`, `observedResponseTargetsPassed`,
`referenceStatus`, `referenceAmplitudeQualified`, `responseQualityQualified` and
`referenceCorrelatedReadinessQualified`. Budget exhaustion is
`ReferenceUnconverged`, not a DDGI failure or a passing quality result. Errors at
the first completed refresh Ready and first steady Ready are reported separately;
these single-frame energy/chromaticity observations do not certify temporal RMS
or redefine estimator Ready as reference convergence. Their diagnostic flag is
independent of settled endpoint/response quality. The contract suite exercises
both analyzers, including missing response, wrong sign, biased but stable energy,
phase provenance and bounded reference exhaustion. Downward/small steps, frozen
F7, G3 exit, TAA-confidence behavior and production performance remain separate.

## Standalone measurements

Measurements are separate from the catalog's correctness regressions. For the
probe update cost workload:

```powershell
& ./Tests/Verification/MeasureGIProbeUpdate.ps1 `
    -Rhi @('dx12', 'vulkan') -Python $python
```

[MeasureGIProbeUpdate.ps1](MeasureGIProbeUpdate.ps1) requires a Release build with
`GGLAB_ENABLE_PIX=0`. It requests hidden native `1920x1080`, effective validation
off, warmup and published 360-frame timing ranges without images, graph/resource
exports or watches inside those ranges. The owning
[cost validator](ValidateGIProbeCost.py) checks the recorded conditions, profiles
and sample coverage before writing `validation.json` and `report.md`. Require the
runner's final `result.json` too: report generation precedes the remaining audits
and source/product identity check.

Stage totals join raw sample indices before median/p95 aggregation. Traversal-only
controls are different workloads, not isolated shading timers. Probe checks and
measurements cover an experimental update chain, not production field or complete
GI performance acceptance. Keep hardware-specific numbers and run history in
experiment reports, not this guide.

### Completed probe change and readiness explanations

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File Tests/Verification/RunVerification.ps1 -Suite gi-probe-change -Configuration Develop -Rhi dx12,vulkan -Python $python
```

The GI Viewer and Host `view.gi_probes.mode=2` select Change. The same frame settings
provide `change_normalized` (default true), `change_range` (default 3) and
`change_blocks` (default false). These controls affect diagnostic presentation.
The heatmap uses completed estimator values and their completed classification,
with a separate current display observation. The block table retains p95/maximum,
warming, quiet hold and all applicable blocker bits. Excluded blocks have no p95
or maximum and never block readiness. Black irradiance can still have nonzero
distance change; `includesDistance` identifies that measurement signal.

`frame.giField.summary.changeState` distinguishes completed, pending, disabled and
failed/unknown completion. `changeObservation` carries immutable CPU values and
the original field/lighting/environment/epoch/update/frame identity. Refresh or
replacement clears current measurement availability. Reading summaries does not
poll fences or advance readiness. Visualization metadata is nested schema v6;
existing observation/counter/ordered-ID layouts retain their versions.

Request `GI.Probes.Change` with a composited image in one capture attempt for a
paired image/data observation. Its v1 ABI is an R32Uint texture, width 256, rows
rounded up to hold packed uint4 records, with zero padding. Header records 0-7:

| Record | Four words |
| --- | --- |
| 0 | magic `0x47494331`, version 1, probe count, block count |
| 1 | field generation low/high, refresh epoch low/high |
| 2 | producing frame low/high, field-lifetime update low/high |
| 3 | epoch-relative update low/high, epoch origin low/high |
| 4 | float bits: tau, 3 tau, irradiance floor, adopted noise |
| 5 | hysteresis float bits, phase bits (1 refresh, 2 includes distance), warming updates, required quiet updates |
| 6 | grid X/Y/Z counts, block width |
| 7 | readiness state ordinal, Active blocks, held-quiet blocks, Excluded blocks |

One record per stable probe ID follows: change float bits, classification (1 Active,
2 Inactive), zero, zero. Then two records per block in `x + Bx*(z + Bz*y)` order:
Active count/p95 bits/maximum bits/quiet count, then blocker bits/warming-met/current-
quiet/zero. Blocker bits are 1 warming, 2 p95, 4 maximum and 8 incomplete hold.
Excluded statistics encode zero in the binary payload and null in JSON, qualified
by Active count zero. Image palette is diagnostic RGB at `t=clamp(value/range,0,1)`:
`clamp((2t, 2-2*abs(2t-1), 2-2t),0,1)`; over-range values saturate.

The suite independently reduces exported probe values, validates block decisions,
identity/pairing, deterministic selection and heatmap pixels on F2/F4/F10 and an
all-Excluded fixture. Off and readiness-disabled controls schedule no Change work.
The immutable upload is replaced only when its completed observation changes and
retained by each reading submission. Its logical bytes are reported separately;
ordinary Classification/Irradiance uses a 16-byte fallback binding. Change also
creates a padded export texture and optional bounded block outlines. These costs
and diagnostic correctness do not establish GI quality or performance acceptance.

### Probe mouse picking

Run `RunVerification.ps1 -Suite gi-probe-picking -Configuration Develop -Rhi dx12,vulkan`.
`TestGIProbePicking.ps1` compares real 1x1 ID/depth exports with an independent
FP64 sphere point rasterizer, including Native/TAA/Quality execution views,
Inactive solid pick silhouettes, display filters and stale-source failures.
Its bundle seal checks source/execution provenance, paired products and injected
contradictions. `TestGIProbePickingDepth.ps1` separately checks exact D32/tie rules,
cropped near/far clipping and full-view GPU controls in bounded capture groups.
The controlled sloped-plane oracle quantizes clipped screen vertices to
`-RasterSubpixelBits` (default 8); qualify that input against the selected adapter.
Both cropped and full-view interpolation must agree with the independent model
within 2e-7. Constant-depth ownership and adjacent D32 values remain exact checks.

Enable probe display, then read `status.frame.probePickingSource` from the last
submitted display. Pass `-GIProbePicking` / `-GIProbePickingFile` with
`{token, pixel:[x,y], source:<copied object>}` and `-ExpectedRunIdentity` to `capture`.
The adapter adds `GI.Probes.PickID` and `GI.Probes.PickDepth` to the same resource
group. NoHit encodes ID zero and clear depth; hits encode stable ID plus one.
Capture metadata exposes the frozen observation on every product and a decoded
Hit/NoHit/MalformedPayload on the resource product. View/settings/field/content/
mapping changes reject before issue. Later jitter/field updates may execute with
matching logical bindings; this observes the execution frame.

The GUI checkbox enables scene clicks with an absolute pointer outside UI windows.
Only the latest matching GUI token applies a completed selection; panel closure
invalidates it, while submitted storage drains through capture. Numeric inspection
keeps its separate explicit job. Picking creates two four-byte logical attachments;
graph execution allocations and resource-export readbacks report native alignment
costs. The ordinary full-view probe depth and optional verification exports are
separate costs. The draw submits the display budget's instance capacity, with
768 vertices per instance; ordered IDs/counts identify the actual retained probes.

### Probe viewer overhead and usability

Run `TestGIProbeViewer.ps1` through the `gi-probe-viewer` catalog suite in Develop.
It captures the full F4/Coastal grids on both backends, independently checks stable
IDs and filter/budget/highlight counts, and exercises native hidden-window resize
and field replacement. The owning `WinAppTests --suite app-devtools-view-profile`
checks the real GUI controls, truncation text, numeric selection/clear, session
precedence and panel closure. Receiver prerequisites/markers have separate graph
allocation evidence. These validation runs provide no performance conclusion.
The viewer starts disabled and inherits a 4096-probe budget. Use
`TestGIProbeViewer.ps1 -DefaultsOnly` for a focused dual-RHI F4/Coastal check that
leaves the budget unassigned and verifies the inherited value reaches GPU work.
Scene Depth can hide retained first-N probes; use the explicit truncation counts,
layers, a larger budget or X-ray when diagnosing an apparently empty display.

`MeasureGIProbeViewer.ps1` requires Release with `GGLAB_ENABLE_PIX=0`. It measures
viewer off, Classification, Irradiance and Classification plus one independent
selected highlight at 1280x720, Native/TAA off, on F4 and the complete 12,870-probe
Coastal volume. Defaults are budgets 256/2048/4096/16384, both depth modes, two repeats
with reversed arm order and bracketing off baselines, and at least 120 verified
submitted warmup frames after each arm and its allocation capture. Allocation
captures use AfterReady so a cold or rebuilt scene starts Ready temporal continuity
before the warmup baseline; a NextFrame view cut during loading is insufficient.
The runner records a Ready settle-counter baseline and waits for the configured additional
submissions with unchanged run/content/parameter/view/field/viewer bindings.
`warmup/submitted-frames.json` retains status observations and start/end serials;
polling can overshoot the requested minimum. A completed next-frame capture does
not establish warmup. `gi-probe-viewer-contracts` tests this without a GPU.
Images and full graph/allocation captures finish outside each timed sequence.
All 360 raw samples remain published; reporting explicitly excludes the first
30 sequence frames to allow profiling and frame-local state to settle.

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File Tests/Verification/RunVerification.ps1 -Suite gi-probe-viewer -Configuration Develop -Rhi dx12,vulkan -Python $python
powershell -NoProfile -ExecutionPolicy Bypass -File Tests/Verification/MeasureGIProbeViewer.ps1 -Rhi dx12,vulkan -Python $python
```

Require the runner's final `result.json` as well as `validation.json` and
`report.md`. Timed ranges reject validation/DevTools/PIX, images, graph/resource
exports, non-finite watches, missing provenance, missing/insufficient submitted
warmup or a timed range preceding that warmup, activation work and incomplete
matrix coverage. Compare within one backend/device/configuration. GPU viewer
totals join scope sample indices before aggregation; frame deltas are differences
of pooled medians against viewer-off baselines. Small frame deltas require repeats.

Sequence `gglab.gpu-timing` v2 adds nullable per-sample
`cpuBuildRecordMilliseconds`, joined to the same submitted RenderFrame serial as
the GPU result, including delayed/out-of-order completions. Production measures
owner-thread elapsed time after RenderHost BeginFrame through render recording,
before EndFrame. It excludes frame-slot acquisition, content Update,
submission/Present and readback tails; it is not whole-process CPU utilization.
Readers retain v1 support; a null CPU value is unavailable and cannot qualify a
viewer cost sample. Visualization summary v5 adds nullable retained native
`readbackAllocatedBytes`; logical ring bytes, private D32 and selection resources
remain separate. Graph allocator totals include other owners and retained pools,
and are not resident memory. B's receiver prerequisites and marker work are
reported separately from C and E's future 1x1 picking. No GI performance,
convergence or readiness acceptance follows from viewer overhead measurements.

## Results and artifacts

- The catalog writes `result.json` and step logs under a unique child of
  `Build/Validation/Verification/`, or the root supplied with `-OutputDirectory`.
  Require process exit zero and `result.ok=true`; inspect failed step receipts/logs.
- Standalone GPU runners use `-OutputDirectory` for a unique child run or
  `-RunDirectory` for an exact new directory. Do not combine them or reuse an
  existing run. Probe numerical/cost defaults are `Build/Validation/GIProbeUpdate/`
  and `Build/Validation/GIProbeCost/`; their final verdict is `status="passed"`.
- Use published bundle manifests and their product paths, rather than guessing
  filenames. Completed/Published means acquisition succeeded; numerical or image
  acceptance comes from the owning checker. For sequences, inspect the requested
  timing/parameter publications as well as sequence completion.
- Inspect saved inputs and frame provenance alongside images, arrays or timings.
  Schema validation alone does not establish numerical or visual correctness.
  Keep native logs, source/product identity receipts and run settings with evidence.

When adding verification, keep suite membership in `RunVerification.ps1`, options
in the script and numerical rules beside the oracle. Add a workflow here only
when its invocation or result interpretation needs explanation.
