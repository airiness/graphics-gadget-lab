---
name: gglab-frame-capture
description: Collect and inspect gglab rendering evidence in the background. Use for image comparisons, session parameter and scheduled-event verification, camera-path replays, GPU timing, RenderGraph/resource exports and non-finite diagnostics.
---

# gglab frame capture

Use hidden capture/session controls from the repository root on Windows with a
supported GPU. Script options belong to
[GGLabSession.ps1](../../../Scripts/Verification/GGLabSession.ps1); use the script
and executable from the same revision (control protocol v4).

## Choose the workflow

| Need | Reference |
| --- | --- |
| One screenshot, settings or image comparison | [Captures and comparison](references/captures.md) |
| Set Host/Lab parameters, pin or release Host overrides, inspect effects | [Session parameters](references/session-parameters.md) |
| Capture an expected frame state, manage jobs or inspect provenance | [Session jobs and artifacts](references/session-jobs.md) |
| Scheduled events, camera paths, references or GPU timing | [Sequences](references/sequences.md) |
| Pass order, barriers or exact texture/buffer values | [Graph and resource exports](references/graph-and-resources.md) |
| NaN/Inf, recovery counters or submitted-frame coverage | [Non-finite diagnostics](references/nonfinite.md) |
| Owner-requested visible/F5 capture | [Interactive capture](references/interactive.md) |

Read only the references needed for the task. For several captures, reuse a
session; `batch` captures authored reference views discovered through `status`.

## Prepare a run

- Build using `AGENTS.md`. Pass `-Configuration Develop` explicitly for routine
  optimized verification with assertions; the session script defaults to Debug.
  Use Debug for additional checks and Release for performance measurements.
- Session `start` is hidden by default; one-shot launches require `--hidden`.
  Use visible windows only when the owner requests or permits them. Use
  `-NoDevTools` for scene/graph comparisons that do not need tooling UI.
- Select stable `-Lab gglab.lab.*` or `-Demo` content ids and `-Rhi dx12|vulkan`.
  Exercise both backends for shared rendering changes.
- Use unique session ids and task-owned `-StateRoot` directories under `Build/`
  when saved settings or shader/IBL caches could affect the experiment. Match
  intended settings across comparison arms and preserve the owner's saved state.

## Capture from a session

This example uses GTAO on DX12. Adapt content/backend to the task; returned
`manifest` and `products[].files` identify the evidence.

```powershell
$session = 'Scripts/Verification/GGLabSession.ps1'
$id = 'verify-' + [Guid]::NewGuid().ToString('N').Substring(0, 12)
$state = Join-Path (Get-Location) "Build/Sessions/$id/State"
$start = powershell -NoProfile -ExecutionPolicy Bypass -File $session start `
    -Session $id -Configuration Develop -Rhi dx12 -Lab gglab.lab.gtao `
    -WindowSize 1280x720 -NoDevTools -StateRoot $state | ConvertFrom-Json
if (-not $start.ok) { throw ($start | ConvertTo-Json -Depth 10) }
try {
    $capture = powershell -NoProfile -ExecutionPolicy Bypass -File $session capture `
        -Session $id -SettleFrames 16 -Label overview | ConvertFrom-Json
    if (-not $capture.ok -or $capture.status -ne 'completed' -or
        $capture.publication -ne 'Published') {
        throw ($capture | ConvertTo-Json -Depth 10)
    }
    $capture | ConvertTo-Json -Depth 10
}
finally {
    $stop = powershell -NoProfile -ExecutionPolicy Bypass -File $session stop `
        -Session $id | ConvertFrom-Json
    $stop | ConvertTo-Json -Depth 10
    if (-not $stop.ok -or $stop.exitCode -ne 0) { Write-Warning 'Inspect session shutdown.' }
}
```

`start` accepting requests does not mean content is ready. Default `after-ready`
captures wait for readiness, then settle submitted frames; inspect pending gates
with `status`. Custom-pipeline Labs `gglab.lab.render_graph_compute` and
`gglab.lab.coordinate_conformance` do not open `ibl`; use `-Timing next-frame`
for smoke captures and check actual content/logs before accepting the image.

## Run repeatable checks

[RunVerification.ps1](../../../Tests/Verification/RunVerification.ps1) owns suite
selection. It defaults to CPU `contracts`; GPU suites require an existing build
and default to Develop on both APIs. Python suites need `jsonschema` and `numpy`;
select an interpreter with `-Python`.

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File Tests/Verification/RunVerification.ps1 -List
powershell -NoProfile -ExecutionPolicy Bypass -File Tests/Verification/RunVerification.ps1 -Group contracts
powershell -NoProfile -ExecutionPolicy Bypass -File Tests/Verification/RunVerification.ps1 -Suite session-parameters -Configuration Develop -Rhi dx12,vulkan
```

Use listed suite names for focused checks; suite/configuration/backend lists accept
comma-separated values. Unsupported combinations reject before launch; injection
requires Debug/Develop. Results and step logs go under `Build/Validation/Verification/`
or `-OutputDirectory`, with nonzero exit on failure. Standalone GPU scripts use
`-OutputDirectory` for a unique child run or `-RunDirectory` for an exact new run
directory; do not combine them. Audits/overhead/baselines remain separate tools.

## Inspect and finish

- Check the requested outcome: Completed + Published captures, completed sequence
  publications, successful parameter effects and any requested watch claims.
  The references define waiting and result consumption for each workflow.
- Open PNGs with the available image-viewing tool; inspect saved metadata for
  content/camera/frame/time, readiness, settings and frozen parameters. Inspect
  `Build/Sessions/<id>/output.log` for assertions, shader/validation failures and
  lifetime/synchronization errors.
- Compare matched inputs and inspect the images, numerical values or timings
  relevant to the task.
- Stop every session you started, including after failure, and inspect `exitCode`.
  Finish the session before linking a new executable. Default logs/captures remain
  under `Build/Sessions/<id>/`.
- Report configuration, backend/content/view or path, inspected artifact paths,
  observed results and material gaps. Keep run-specific results out of this skill.
