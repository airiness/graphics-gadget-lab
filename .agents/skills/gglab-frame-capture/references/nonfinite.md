# Non-finite diagnostics

Boundary scans inspect stored resource values. Production counters detect
sanitization/recovery and reviewed finite-clamp policy violations. Clean boundary
values alone cannot prove that no non-finite input was replaced before storage.

Use `$session = 'Scripts/Verification/GGLabSession.ps1'`. Start the intended configuration with
`-WatchNonFinite` when production evidence is needed:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File $session start -Session numeric-check `
    -Configuration Develop -Rhi dx12 -Lab gglab.lab.gtao -NoDevTools -WatchNonFinite
powershell -NoProfile -ExecutionPolicy Bypass -File $session watch -Session numeric-check -WatchMode fail
powershell -NoProfile -ExecutionPolicy Bypass -File $session capture -Session numeric-check -Label checked
powershell -NoProfile -ExecutionPolicy Bypass -File $session status -Session numeric-check
powershell -NoProfile -ExecutionPolicy Bypass -File $session stop -Session numeric-check
```

Use the cleanup pattern in `SKILL.md` for scripted runs. Startup instrumentation
selects counter variants before pipeline creation; it does not turn on boundary
watch/reporting by itself. Unsupported Vulkan pixel-storage atomics reject an
instrumented host. A fail request requiring absent instrumentation returns
`RequiresRestart` without changing configuration.

## Configure and inspect watch

`watch` defaults to report mode, semantic boundary targets, and both `boundary`
and `production` claims. Configuration replaces the previous one, increments
`configurationVersion` and resets report history/totals. Invalid requests preserve
the previous configuration. Examples for an existing session:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File $session watch -Session numeric-check -WatchClaims boundary
powershell -NoProfile -ExecutionPolicy Bypass -File $session watch -Session numeric-check `
    -Targets '[{"resource":"Temporal.MotionVectors"},{"resource":"GTAO.TemporalAO"}]'
powershell -NoProfile -ExecutionPolicy Bypass -File $session watch -Session numeric-check -WatchOff
```

Explicit targets share resource/pass/mip/slice addressing with exports.
`-TargetsFile` reads the same array. Texture scans accept supported floating-point
views, not integer/normalized/stencil or ambiguous typeless interpretations.
Imported textures require Sampled usage; imported buffers require shader-readable
Structured usage (resource export separately requires CopySource).

Buffers need an owned interpretation and producer-valid element count:

```json
[{"resource":"Values","buffer":{"stride":16,"validElementCount":3,"fields":[
  {"offset":0,"type":"uint32"},
  {"offset":4,"type":"float32","count":2},
  {"offset":12,"type":"float16","count":2}
]}}]
```

Only float fields are scanned; integers, padding and inactive capacity are
excluded. Types are `uint8`, `uint32`, `float16` and `float32`, count defaults to
one. Limits are 256 fields and 4096 float components per element. Interpretation
is rechecked against the captured frame's descriptor.

Read `status.watch` and `watch.report` for instrumentation availability, bound
configuration, separate boundary/production serials, per-target counts/extrema
and first anomaly. Scalar indices identify the earliest checked scalar, not the
origin of production. `firstAnomaly` retains the earliest observed failure since
configuration; `siteTotals`/`targetTotals` aggregate observed evidence for audits.
The latest clean frame does not erase an earlier observed failure.

`-WatchOff` stops boundary requests/report tracking, but an instrumented process
continues collecting counters. Restart without `-WatchNonFinite` for baseline
performance measurements.

## Defaults and interpretation

Tier A observes applicable motion vectors, GTAO before upsampling, scene color
before Temporal AA and before the earliest live post-process consumer. Tier B
covers active IBL cubes (every mip/face) and atmosphere LUTs by content generation.
Use registered semantics and exact versions; matching resource names alone do
not establish coverage. GI Tier C defaults are not provided.

Inactive features are `NotApplicable`; missing/culled/conflicting active bindings
are `Incomplete`/`MissingCoverage`. Generation records include `generation`,
ephemeral `resourceIdentity` and `lastCheckedFrameSerial`. Unchanged, previously
checked clean content can be `NotApplicable` with retained older evidence;
pending revisions remain `Incomplete`. A stale generation cannot certify new
content. A failed generation remains failed while active.

Transport completion is distinct from a passing claim. Non-finite replacement
fails production. Finite clamps follow the site's reviewed policy, so a nonzero
clamp count does not automatically fail. Read the authoritative inventory in
[NonFiniteSites.json](../../../../Shaders/Metadata/NonFiniteSites.json); do not
change policy merely to make a diagnostic run pass.

## Submitted-frame coverage

A capture binds watch coverage to its exact submitted frame. A sequence binds
configuration at frame 0 and covers every submitted frame, including uncaptured
frames and all reference samples. Inspect the range's `watch` record rather
than substituting the interactive latest-frame report.

Captures wait for payloads and requested claims; sequences retain a finishing
tail. Reconfiguring/disabling watch invalidates unfinished ranges as Incomplete.
Pending evidence expires after 64 further successful runtime submissions or
30 seconds. A late anomaly can still fail the sequence.

Report mode publishes artifacts even with failed/incomplete claims; capacity
pressure can create reported gaps. Fail mode defers temporary pressure before
submission, and fails impossible budgets or failed/missing evidence explicitly.
Failures include `NonFiniteDetected`, `NonFiniteReplaced`, `ClampPolicyExceeded`
and `MissingCoverage`. The process stays alive for inspection.

Requested production without instrumentation is `Unavailable` in interactive
capability status and `Incomplete` in submitted-range coverage. Unrequested
claims remain Unavailable. Validate nested `gglab.nonfinite-watch` v2 records:

```powershell
python Scripts/Verification/ValidateNonFiniteCoverage.py Build/Sessions/numeric-check/Captures
```

The schema remains [NonFiniteWatch](../../../../Schemas/NonFiniteWatch.schema.json).

## Controlled injection

Debug/Develop permit injection; Release rejects it. It requires startup
instrumentation and a requested production claim, even for a boundary probe.
Discover point ids in the inventory's `injectionPoints`. Values are `NaN`,
`PositiveInfinity`, `NegativeInfinity`, `Overflow` (131072) and `Negative` (-4).
For an instrumented `gglab.lab.temporal_aa` session named `taa-inject`:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File $session watch -Session taa-inject `
    -WatchMode fail -WatchClaims production `
    -Inject '{"point":"pass-temporal-aa.csmain.history","value":"NaN","x":161,"y":93,"component":1}'
```

History components 0-2 are RGB, 3 accumulation. The compute Lab also registers
sanitized preview and
RGBA32F probe points; use `-Timing next-frame` there because it has no IBL bake.
Require matching injection stamps and applied counts in reports/coverage; clean
PNG pixels can coexist with a production failure after sanitization. Debug and
Develop also accept startup `-LabParameters 'id=value'`; Release rejects them.

## Focused audits and regression gates

Select regressions with `Tests/Verification/RunVerification.ps1 -List` and
`-Suite <name>`; see [Run repeatable checks](../SKILL.md#run-repeatable-checks).
Build GPU suites' required executables first. The scripts below also run
independently; inspect their parameter blocks for individual defaults.

| Script | Evidence |
| --- | --- |
| `Scripts/Verification/AuditNonFiniteLabs.ps1 -Rhi dx12 -Content gtao` | Hidden per-content report/fail audit, paths or observation interval, site/target totals. |
| `Scripts/Verification/MeasureNonFiniteOverhead.ps1` | Same paths in baseline, counters-only and counters+watch arms; compare within backend/run. |
| `Tests/Verification/TestNonFiniteCoverage.ps1` | Capture/sequence ranges, missing capability/targets and failure lifecycle. |
| `Tests/Verification/TestNonFiniteGenerations.ps1` | Fresh/cache-restored IBL, mip/face coverage, reconfiguration and atmosphere revisions. |
| `Tests/Verification/TestNonFiniteInjection.ps1` | Injected anomalies, locations/counts, stamps and clamp policy. |
| `Tests/Verification/TestTemporalAACounters.ps1` | Counter off/on same-frame PNG and NPY equivalence plus watch/lifecycle checks. |

Audit and overhead results go to unique directories under `Build/Validation/`.
Audits gather policy evidence; report-mode ranges may be incomplete. A clean
watch report does not establish off/on image/value equivalence or low overhead.
For CPU/GPU diagnostic implementation changes, also select the owning Runtime,
AppRuntime or ShaderRuntimeIntegration test suites from their runners. Generated
site inventory checks remain `GenerateNonFiniteSites.py --check` and
`ValidateNonFiniteSites.py` under `Scripts/`.
