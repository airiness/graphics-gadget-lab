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
| `gi-field-baseline` | Feedback-off D=1 reference accuracy and 120 consecutive Ready-plus-120 observations |
| `gi-field-feedback` | Previous-field transport, safe toggles, support accounting and age-one consumption |
| `gi-field-light-step` | Published-lighting response; see below |
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

Baseline accuracy, chromaticity and temporal RMS are separate verdicts. Optional
`TestGIFieldBaseline.ps1 -DiagnosticReadyOffsets 512,1024,2048` records later
windows without replacing the primary Ready-plus-120 gate. Feedback-on accuracy
requires a matching sample- and depth-qualified run supplied through
`TestGIFieldFeedback.ps1 -ReferenceDirectory`; D=1 is insufficient.

Baseline, feedback and lighting-step runners accept `-ShaderCacheDirectory` with
matching `<root>/<rhi>/ShaderCache` and `ShaderArtifacts` seeds. They copy artifacts
into isolated state and preserve normal identity validation.

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

For engineering attribution, use `-TransientAttribution -Tap gi -Feedback off` with
small-step recipes. `-TraceSeed 61052,71052,81052` and
`-PreEventSettleFrames 128,145` expand repeats; waits are minima, not update-phase
locks. This mode exports paired atlas/ray/query data and cannot request references
or Final. [AnalyzeGIFieldTransient.py](AnalyzeGIFieldTransient.py) performs receiver
closure, atlas/query-weight decomposition and bounded Trace/Blend checks. Historical
analysis requires a fresh `--output`; raw inputs and failures remain preserved.
Engineering completion, arithmetic contracts and response quality retain separate
verdicts. Full F7, G3 and production qualification require their own gates.

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
