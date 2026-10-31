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
- Select a Python environment with `jsonschema` and `numpy`; image-based checks
  may also require `Pillow`. Pass its executable through `-Python`.
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
and include rules. Map maintenance belongs to the
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
