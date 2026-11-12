# Verification

[RunVerification.ps1](RunVerification.ps1) owns the suite catalog and supported
configurations. Script parameters and the numerical validators own individual
options and checks. Run from the repository root in PowerShell.

## Prerequisites

- Build with the MSVC settings in [AGENTS.md](../../AGENTS.md#build-and-verification).
  Stop owned application sessions before linking; avoid concurrent IDE builds.
- Select Python with `numpy`, `jsonschema` and `Pillow`, and pass it through `-Python`.
- Use Develop for GPU correctness, Debug for additional checks and Release for cost
  measurements. Shared rendering changes require DX12 and Vulkan.

## List and run suites

```powershell
$python = 'python' # Or the absolute path to a configured interpreter.
powershell -NoProfile -ExecutionPolicy Bypass -File Tests/Verification/RunVerification.ps1 -List
powershell -NoProfile -ExecutionPolicy Bypass -File Tests/Verification/RunVerification.ps1 `
    -Group contracts -Python $python
powershell -NoProfile -ExecutionPolicy Bypass -File Tests/Verification/RunVerification.ps1 `
    -Suite gi-field -Configuration Develop -Rhi dx12,vulkan -Python $python
```

The default `contracts` group is GPU-free. GPU suites require existing application
and shader-compiler builds. The catalog accepts comma-separated suite, configuration
and RHI lists. For standalone scripts, use their declared parameters; PowerShell
array parameters can require actual arrays such as `-Rhi @('dx12', 'vulkan')`.

## Repository checks

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CheckArchitectureAtlas.ps1 -RootDir .
powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/ValidateProjectBoundaries.ps1 -RootDir .
powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/GenerateProjectFilters.ps1 -RootDir . -Check
```

The Atlas helper is report-only: inspect navigation, publication and incomplete
impact findings even when it exits zero. Ownership changes require the generation
commands in [AGENTS.md](../../AGENTS.md#source-and-generated-files).
Native tests belong to their [owning targets](../../Docs/Architecture/Module_Catalog.md);
the [CI workflow](../../.github/workflows/build.yml) contains executable commands.

## GPU regressions

Use hidden, task-owned sessions and fresh output/state directories. Match scripts,
binaries and shader artifacts. Check the numerical verdict, artifact audits,
source/binary seals and native logs. Stop every session the run creates.
For ad hoc captures and session controls, follow
[gglab-frame-capture](../../.agents/skills/gglab-frame-capture/SKILL.md).

`startup-readiness` repeats hidden GI Reference Lab startup, Ready, capture and
normal exit on both RHIs. Each warm run uses only caches from its paired cold run;
optional `TestStartupReadiness.ps1 -ShaderCacheDirectory` seeds shader artifacts
and explicitly excludes shader-cold qualification. Failures after ownership is
confirmed retain a thread/context minidump before shutdown; forced cleanup stays
failed. `process-diagnostics` checks dump integrity and noninterference without a GPU.

### TAA comparison

`taa-comparison` uses [Invoke-TaaComparison.ps1](../../Scripts/Verification/Invoke-TaaComparison.ps1)
for paired images/graphs and Host override restoration. Inspect `result.json`,
`journal.json` and `comparison.json`: executed passes, restoration and image quality
are separate observations. See the
[guarded workflow](../../.agents/skills/gglab-frame-capture/references/captures.md#guarded-taa-comparison).

### GI checks

[GI inputs and reference contracts](GlobalIllumination/README.md) describe frozen
fixtures, resource meanings and reference qualification. Select the relevant suite:

| Suite | Scope |
| --- | --- |
| `gi-receiver-cache` | Bounded preparation, partition invariance and resolve-only reuse; standalone `-Coastal` adds primary matching, `-BaselineDirectory` adds byte-exact controls |
| `gi-probe-update` | Ray integration, scheduling, history, lifecycle and allocation diagnostics |
| `gi-field` | Static full-field update and lifecycle |
| `gi-field-sampling` | FP64 receiver interpolation, support/coverage, Forward composition and GI-off controls |
| `gi-field-readiness` | Completed-update change reduction, calibrated refresh/steady noise and capture gating |
| `gi-field-readiness-repeat` | Hard-reset Ready counts over trace seeds and a same-seed repeat; see below |
| `gi-field-baseline` | Feedback-off D=1 reference accuracy and 120 consecutive Ready-plus-120 observations, with F3/F5 limits |
| `gi-field-feedback` | Previous-field transport, safe toggles, support accounting and age-one consumption |
| `gi-field-light-step` | Published-lighting response; see below |
| `gi-field-camera-independence` | F6 held pose-specific fields, per-view lookups and the frozen receiver list; see below |
| `gi-invalid-updates` | Numerical failure and production-consumer rejection |
| `gi-probe-inspection`, `gi-receiver-inspection` | Bounded numeric observations and receiver contributions |
| `gi-probe-selection` | Generation-bound highlights and budget isolation |
| `gi-probe-visualization`, `gi-probe-depth`, `gi-probe-temporal` | Spatial display and depth/temporal-domain contracts |
| `gi-probe-change` | Completed change heatmaps and readiness blockers |
| `gi-probe-picking` | Logical-view-bound 1x1 ID/depth picking and stale-request rejection |
| `gi-probe-viewer` | Viewer controls, deterministic budgets, resize and field replacement |

The catalog lists corresponding GPU-free contract suites. `-Smoke`, `-SkipCoastal`
and standalone case selectors establish only their selected subset.

Readiness means observed stationarity, not reference accuracy. The readiness runner
retains its uncalibrated result, measures refresh/steady noise, then explicitly
adopts those inputs with another seed. Recalibrate after sampling/history changes.
Readiness-disabled suites use the fixed-settling diagnostic path.

Each refresh reason has its own integration and calibration. By default a hard reset
integrates irradiance progressively after its entry update, and a lighting refresh
integrates it at the refresh hysteresis 0.5. The hard-reset inputs are
`field_refresh_noise` and `field_steady_noise`; the lighting inputs are
`field_lighting_refresh_noise` and `field_lighting_steady_noise`. A noise value is the
largest per-probe RMS change in one phase's calibration window, not a stationary-noise
certificate. Field profile schema 2 declares both reasons; schema 1 roots declared one
policy for both. [GIFieldReadinessCalibration.psm1](GIFieldReadinessCalibration.psm1)
and [GIFieldReadinessCalibration.py](GIFieldReadinessCalibration.py) hold the shared
calibration steps and checks. The readiness, baseline, readiness repeat, feedback and
light-step runners take `-SteadyCalibration`. The readiness repeat records it in its
`recipe.json`, the others in `readiness-calibration.json`. Runners without lighting
refreshes leave the lighting inputs at zero. The default `after-handoff` keeps
refresh noise from the calibration run, whose uncalibrated refresh ends at the
timeout. It then measures steady noise in a second field at seed 61051 that adopts
that refresh noise, so its steady window starts at a calibrated handoff, as the
gated runs' does. A calibration run whose refresh noise is zero, such as a field
without active blocks, already handed off at that sigma; its unchanged inputs would
not restart a second field, so its `steady-calibration.json` reuses that run and says
so. `after-timeout` adopts the calibration run's steady noise. Roots without the
recipe calibrated after the timeout.

Baseline accuracy, chromaticity and temporal RMS are separate verdicts. Optional
`TestGIFieldBaseline.ps1 -DiagnosticReadyOffsets 512,1024,2048` records later
windows without replacing the primary Ready-plus-120 gate. Feedback-on accuracy
requires a matching sample- and depth-qualified run supplied through
`TestGIFieldFeedback.ps1 -ReferenceDirectory`; D=1 is insufficient.
For the opt-in contiguous-sequence experiment, select
`-FieldRecipe progressive-lighting-sampling` or `-FieldRecipe rapid-control-sampling`.
The latter uses rapid hard-reset integration until Ready/Unconverged, while the
former grows real irradiance history. The runner records the selected policies,
reconstructs F1 hit directions accordingly and requires a source/binary-matched
fresh F4 reference. `default` keeps progressive reset, rapid lighting and rotated
Fibonacci directions. Feedback-off captures are settled toggle controls, not complete paired
single-bounce baseline qualifications. Static and lighting-response qualification
remain separate; neither admits combined-policy Final or attribution.

Baseline F3 cases cover the thin-wall interior back wall, floor and ceiling plus
the exterior roof, whose D=1 reference must equal pi L. F5 cases cover the
doorway strip and the supplementary right-floor/far-corner view. F5 gates
ray-traced/raster receiver correspondence on the union of its declared masks and
reports the whole-image result, whose far left-room floor exceeds the bound. F3 and
F5 are measured at G3 and gated at G4: their G3 case verdict is readiness and
reference qualification. Their stability, energy and leakage results are G4
previews outside it, the latter two written to `limits.json` by
[ValidateGIFieldLimits.py](ValidateGIFieldLimits.py).
Stability against a dark or zero reference and the local-leakage metric
`(GI - ref) / max(ref, floor)` need a predeclared floor. `-LimitPolicy` supplies
per-fixture `stabilityFloor`/`leakageFloor` values: absolute luminance, or
`skyIrradiance`, the irradiance pi L of the fixture's frozen constant sky read from
the run's profile receipt. [GIFieldLimitPolicy.json](GIFieldLimitPolicy.json)
declares `skyIrradiance` for both fixtures. Without a policy, a stability result
that passes at the numerical floor still passes, others remain undetermined, and
leakage is reported over a floor sweep, never as a verdict. The F3 sweep scales its
roof reference and the F5 sweep the frozen sky's pi L: at D=1 the F5 right room
receives light only from that sky, through the doorway. Field and reference are
both single-bounce, so F5 excess over its reference is field error, reported
absolutely and against that basis.

### Readiness repeatability

[TestGIFieldReadinessRepeat.ps1](TestGIFieldReadinessRepeat.ps1) runs the default
single-bounce recipe for F1 sphere and plane, F2, F3, F4, F5 and F6 (the F4 field
from the F6 west pose). It enables the observation-only `field_readiness_trace`
parameter. The runtime then keeps every readiness update of the field's epoch, up
to the 240-update timeout, in the status and capture summary as `readinessTrace`.
Each record holds the update's snapshot and its block change measures and
decisions. Sequences cannot observe a field that is still settling, because they
require every readiness gate to stay Ready.

Per case the runner calibrates refresh/steady noise at seed 61051 from the
uncalibrated minimum, then adopts the measured noise. Each trace seed (default
61052, 71052, 81052) and a final same-seed repeat (`-RepeatSeed`, default 61052)
hard-resets the field. The rebuild restores the fixture's first view, so a
next-frame capture then poses the case's reference view. Its metadata records the
run's complete inputs and the field update the pose reached. The runner records
the status once the trace window is full and requires the pose to have held.

The root's `recipe.json` declares the arm: `hardResetIntegration` and
`steadyCalibration`. The defaults are the progressive hard reset and after-handoff
calibration; `-FieldRecipe rapid-control` selects the rapid hard reset. For after-handoff the
validator checks the second calibration field's inputs, fresh field and replayed
trace, and reports its handoff. It also reports whether its refresh change measures
equal the calibration run's. Roots from before per-reason policy declared one
`progressiveRefresh` flag for both reasons and still validate.

[ValidateGIFieldReadinessRepeat.py](ValidateGIFieldReadinessRepeat.py)
independently replays the runtime's threshold, warming, quiet-hold, blocker and
state rules from the traced change measures. It requires every traced decision to
match, including the refresh-to-steady handoff. The 120-update limit counts the
steady Ready that the field holds after the handoff; each seed's first Ready, which
ends the refresh window, is reported as the handoff diagnostic. While a refresh is
open, the runtime settles the readback of the last submitted update before it
records the next one, so the steady policy starts exactly one update after the
update that ended the refresh. The validator reports each run's handoff latency,
which must be one, and the summary's `refreshWaits` cost of that settlement. The
repeat comparison covers the Ready update, per-update non-quiet blocks and change
measures until the two runs first measure in different phases. A steady handoff at
a different update after the same first Ready is a determinism finding. Otherwise,
without `--change-tolerance` from the acceptance plan, only exact equality resolves
determinism. Ready counts never qualify reference accuracy or stability.

Baseline, feedback, lighting-step and readiness-repeat runners accept
`-ShaderCacheDirectory` with matching `<root>/<rhi>/ShaderCache` and
`ShaderArtifacts` seeds. They copy artifacts into isolated state and preserve
normal identity validation.

### F6 camera independence

[TestGIFieldCameraIndependence.ps1](TestGIFieldCameraIndependence.ps1) runs Develop
F6 on frozen F4 with feedback, progressive sampling and progressive refresh off.
For each `-Readiness` arm (default `off,on`), one field per pose restarts from
identical inputs. The diagnostic `field_update_limit` (default 3072) holds it, so
every later frame samples the last update. The pose takes effect at a recorded
early update. At the held update, both poses export their raster lookup group and
`GI.Field.ReceiverList`. This compute query evaluates the frozen 9x7 floor lattice
from both pose positions on one field state, so only the view direction differs.
`-RefreshNoise`/`-SteadyNoise` pass calibrated readiness inputs; zero is the
uncalibrated minimum.

[ValidateGIFieldCameraIndependence.py](ValidateGIFieldCameraIndependence.py)
requires the following contracts:

- The hold at the declared update.
- One field generation across the camera switch.
- Each view, raster and listed, against the FP64 lookup oracle (2e-3 scaled E).
- Each listed sample's cross-view difference against the oracle-predicted
  view-bias difference.
- Visibility from primary identities.
- Each pose's D=1 reference confirming the analytic floor oracle within 1%.

It reports, without gating, world-field atlas and query differences between the
pose-specific runs, along with energy and chromaticity against the shared analytic
reference (lattice) and each pose's matched D=1 reference (raster). Per-quantity
tolerances belong to the acceptance plan. Passing does not qualify F6.

### Integration numerical inspection

`TestGIProbeInspection.ps1 -IntegrationDiagnostics` adds selected-probe arithmetic
captures on both RHIs. `gi-probe-integration-contracts` runs its CPU tests.
The default-off `integrationReplayUpdate` request selects current-update directions
with zero, or a one-based diagnostic replay with a positive value. It does not
change production Trace, field history, readiness or the actual update index.
Request both `GI.Field.ProbeInspection` and `GI.Field.IntegrationInspection` in the
same capture; see [session jobs](../../.agents/skills/gglab-frame-capture/references/session-jobs.md).

[ValidateGIProbeIntegration.py](ValidateGIProbeIntegration.py) decodes the versioned
R32Uint witness and checks observed directions/weights, FP64 integration, numerator
division and Blend separately. M is irradiance divided by pi. Compact values remain
schema v1 (20,496 bytes maximum); opt-in values use v2 and the witness uses ABI v1
(3,784,704 bytes maximum). Layout and decoding are colocated with the validator.

`-HistoricalPoint <point.json> -HistoricalRoot <collection>` uses immutable historical
rays/history for counterfactual analysis in a fresh output directory. Conditional
arithmetic success does not relabel the original failure or qualify transport,
response or a historical binary. Native admission/serialization checks use
`GGLabAppRuntimeTests.exe --suite gi-probe-inspection`; require its completion banner.

### Static-geometry published-lighting response experiment

[TestGIFieldLightStep.ps1](TestGIFieldLightStep.ps1) enables the explicit Physical
Sky experiment in static F4 geometry. This has its own
[lighting recipes](GIFieldLightStepRecipes.json), separate from frozen F7.
`-Step` selects increase/decrease and small-increase/small-decrease; feedback off/on
and GI-only/Native TAA Final are separate replays. The 360-frame path requests its
event at frame 30 and measures from the receipt's actual `firstConsumed`.

```powershell
& ./Tests/Verification/TestGIFieldLightStep.ps1 `
    -Configuration Develop -Rhi @('dx12', 'vulkan') -Tap @('gi', 'final') `
    -Feedback @('off', 'on') -Step @('small-increase', 'small-decrease') `
    -QualifyReferences -Python $python -RunDirectory Build/Validation/LightStep/MyRun
```

`-QualifyReferences` acquires independent endpoints within the same sealed cohort.
GI-only uses scene-referred E; Final uses linear TAA output divided by pre-exposure.
Every required GI ROI needs reference sign, at least half its amplitude, and qualified
settled endpoint accuracy/stability. Timing uses each recipe's own endpoints:
45 GI-only / 60 Final frames, a 30-frame confirmation hold and 10% overshoot.
Inspect `contractsPassed`, `observedResponseTargetsPassed`, `referenceStatus`,
`referenceAmplitudeQualified`, `responseQualityQualified` and
`referenceCorrelatedReadinessQualified` separately in `validation.json`.

The event retains field/scene/history and uses published lighting banks. The narrow
sequence refresh allowance covers only its matching consumed lighting transaction,
ends at steady Ready, and requires refresh Ready plus the completed steady handoff.
Unrelated Pending/Failed gates and later readiness loss still fail.

Per case the runner calibrates the hard reset at seed 61051 as the other runners do.
It calibrates the lighting refresh on a second field at that seed: the field adopts
the hard-reset calibration, holds steady Ready for the pre-event wait and then takes
the case's own step. Under after-handoff a third field repeats this step with the
measured lighting refresh noise to measure lighting steady noise. The measured run
adopts both reasons' calibration. The validator checks every calibration field and
step. `-FieldRecipe` selects an explicit integration/sampling composition from
[GIFieldExperimentRecipes.json](GIFieldExperimentRecipes.json). The default keeps
progressive hard-reset integration, rapid lighting refresh and rotated Fibonacci
directions. `rapid-control` restores rapid hard resets as well.

For engineering attribution, use `-AttributionMode fixed -Tap gi -Feedback off` with
small-step recipes. `-TraceSeed 61052,71052,81052` and
`-PreEventSettleFrames 128,145` expand repeats; waits are minima, not update-phase
locks. This mode exports paired atlas/ray/query data and cannot request references
or Final. [AnalyzeGIFieldTransient.py](AnalyzeGIFieldTransient.py) performs receiver
closure, atlas/query-weight decomposition and bounded Trace/Blend checks. Historical
analysis requires a fresh `--output`; raw inputs and failures remain preserved.
Engineering completion, arithmetic contracts and response quality retain separate
verdicts. Full F7, G3 and production qualification require their own gates.

Live Lab input `gi_reference.field_experiment_recipe` replaces the three switches
`field_rapid_hard_reset`, `field_progressive_refresh` and `field_progressive_sampling`.
All eight former combinations remain available; the default and numerical policies
are unchanged. The replaced Lab IDs and runner switches reject rather than aliasing
another recipe. Existing saved assignments must migrate to the enum.

| `-FieldRecipe` | Enum | Hard reset / lighting refresh | Progressive directions |
| --- | --- | --- | --- |
| `default` | 0 | progressive / rapid | off |
| `rapid-control` | 1 | rapid / rapid | off |
| `progressive-lighting` | 2 | progressive / progressive | off |
| `rapid-reset-progressive-lighting` | 3 | rapid / progressive | off |
| `progressive-sampling` | 4 | progressive / rapid | on |
| `rapid-control-sampling` | 5 | rapid / rapid | on |
| `progressive-lighting-sampling` | 6 | progressive / progressive | on |
| `rapid-reset-progressive-lighting-sampling` | 7 | rapid / progressive | on |

For assignments from the replaced per-reason interface, the enum is
`rapid_hard_reset + 2 * progressive_refresh + 4 * progressive_sampling` using
0/1 switch values. Older schema-1 profiles retain their own historical interpretation
in the read-only validators. Raw roots and failed verdicts are preserved.

The light-step runner admits all recipes within its existing study scope.
Progressive directions require one feedback arm; with rapid lighting they require
feedback-on. Feedback-on progressive-direction studies remain GI-only, without
attribution. Seed/wait selection and reference gates retain their existing rules.
The feedback runner admits `default`, `progressive-lighting`,
`rapid-control-sampling` and `progressive-lighting-sampling`; readiness repeat admits
`default` and `rapid-control`; probe integration inspection admits `default` and
`progressive-sampling` (the latter requires `-IntegrationDiagnostics`).

`-FieldRecipe progressive-lighting` selects one fast lighting-entry update followed
by real history growth at steady decay; `progressive-lighting-sampling` adds the
Sobol directions. Noise calibration, epoch coefficients, distance policy, ray
budget and response/reference gates retain their original semantics. A run writes
`experiment-recipe.json`; existing receipt flags are derived compatibility fields
for the historical arithmetic oracles. The sample index still follows submitted
global updates, not a lighting epoch or CPU-completion snapshot.

Pair progressive lighting with `-AttributionMode progressive` for the bounded eight-payload engineering
exports. This mode derives each Trace/Blend coefficient from the actual GPU epoch
update, retains arithmetic/response failures, and excludes independent references,
feedback-on and Final. History/current-sample splits are labeled derived inversions.
Use `-AttributionStartFrame 1` only with this explicit mode to include pre-event
rays when matching an earlier GPU sampling phase. The default starts at frame 29;
all modes stop the continuous export window at 80 and also observe frame 359.
Frame 0 retains its image slot. Earlier exports do not lock the sampling phase.

`gi-field-experiment-recipes-contracts` tests acquisition admission without launching
a process. Develop GPU suite `gi-field-experiment-recipes` checks the Lab descriptor,
all eight applied profiles, atomic rejection of retired inputs, and same-frame
image/graph/resource publication on both backends. It does not qualify GI quality
or retire any unresolved comparison.

[CompareGIFieldTransient.py](CompareGIFieldTransient.py) joins an uninstrumented
progressive case to compatible attribution replays by exact GPU update and trace
seed, never by wait minimum or sequence frame. It independently integrates rays
for every contributing cage probe, including octahedral borders, and verifies
receiver Blend closure before comparing historical sample inversions. Product
source/binary cohorts, effective inputs, receiver positions and fixed observed
query weights must match. Use `--observed-case <directory>` to select one explicit
compatible arm when a collection also contains differently calibrated inputs;
this does not relax input checks. Frozen feedback-off Physical Sky illumination scaling
is a conditional engineering attribution, not a historical Trace/Blend observation
or independent transport reference. Reports retain the original response failure
and list missing samples before the peak; incomplete matches cannot establish a
complete historical replay. A fully matched window additionally replays the scalar
receiver signal from one measured pre-event value and the independently integrated
rays, without reusing subsequent historical receiver values as history. This still
assumes fixed historical query weights, not a directly observed historical atlas.
Inputs are sealed; analysis code is sealed and copied into a fresh output directory.

```powershell
& $python Tests/Verification/CompareGIFieldTransient.py `
    --historical-root Build/GP/Matrix --root Build/GPA/Before `
    --case vulkan/small-increase-off-gi-s81052-p145 `
    --output Build/Validation/LightStep/MatchedUpdates
```

`-DerivedDataCacheDirectory` optionally seeds `<root>/<rhi>/DerivedDataCache` into
isolated state for warm-start checks; those runs do not qualify cold startup.

## Standalone measurements

| Entry point | Measurement |
| --- | --- |
| [MeasureGIProbeUpdate.ps1](MeasureGIProbeUpdate.ps1) | Experimental update-chain cost and allocation facts |
| [MeasureGIProbeViewer.ps1](MeasureGIProbeViewer.ps1) | Viewer on/off costs, budgets, depth modes and selected highlights |

Both require Release with `GGLAB_ENABLE_PIX=0`, validation/DevTools off and no
images, exports or watches inside timed ranges. Viewer warmup must establish at
least 120 additional submitted Ready frames with unchanged bindings; NextFrame
completion alone is insufficient. Images/allocation captures precede timing.
Raw samples remain available; the viewer report excludes the first 30 sequence
frames. GPU totals join sample indices before aggregation. CPU build/record time
excludes slot acquisition, content Update, EndFrame/Present and readback tails.
Allocation facts are not resident memory. Require final `result.json` alongside
`validation.json` and `report.md`; reports can precede failed final audits.

## Results and artifacts

- Catalog runs write `result.json` and step logs under `Build/Validation/Verification/`
  or `-OutputDirectory`. Require exit zero and `result.ok=true`.
- Standalone runners accept `-OutputDirectory` for a unique child or `-RunDirectory`
  for an exact new directory; never combine them or reuse an existing run.
- Completed/Published proves acquisition. Numerical, visual and performance verdicts
  come from the owning checkers. Inspect native logs and exact input/frame identities.
- Use manifests and product paths. Retain raw arrays, receipts, source/binary seals
  and failed attempts with the evidence. Hardware results and experiment history
  belong in run reports and the owning design records.

When adding verification, keep catalog membership in `RunVerification.ps1`, options
in scripts and numerical rules beside their oracles. Document only invocation and
result interpretation that cannot be inferred from those entry points.
