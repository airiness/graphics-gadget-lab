---
name: gglab-frame-capture
description: Capture and inspect gglab rendering in the background. Use for visual verification of graphics changes, DX12/Vulkan image comparisons, camera-path replays, RenderGraph or GPU resource exports, and non-finite diagnostics.
---

# gglab frame capture

Use the application's hidden capture and session controls to collect rendering
evidence without disturbing the desktop. Run commands from the repository root
on Windows with a supported GPU and the matching application build.

## Choose the workflow

| Need | Workflow / reference |
| --- | --- |
| One screenshot, or capture settings and image comparison | [Captures and comparison](references/captures.md) |
| Several captures or settings from one process | Session example below |
| Every authored reference view | Session `batch`; discover views with `status` |
| Temporal behavior, supersampled references or GPU timing | [Sequences](references/sequences.md) |
| Pass order, barriers or exact texture/buffer values | [Graph and resource exports](references/graph-and-resources.md) |
| NaN/Inf, sanitization counters or submitted-frame coverage | [Non-finite diagnostics](references/nonfinite.md) |
| Owner-requested visible/F5 capture | [Interactive capture](references/interactive.md) |

Read only the reference needed for the current verification. Keep script options
authoritative in [GGLabSession.ps1](../../../Scripts/Verification/GGLabSession.ps1), and use
the script and executable from the same revision (control protocol v2).

## Prepare a run

- Build the intended configuration using the repository's `AGENTS.md` commands.
  Pass `-Configuration Develop` explicitly for routine optimized verification
  with assertions; the session script itself defaults to Debug. Use Release for
  performance measurements and Debug when its additional checks are needed.
- Keep launches hidden. Session `start` does this by default; one-shot launches
  require `--hidden`. Use visible windows only when the owner requests or permits
  them. Use `-NoDevTools` for scene/graph comparisons that do not need tooling UI.
- Select content with its stable `-Lab gglab.lab.*` id or `-Demo` id, and choose
  `-Rhi dx12` or `-Rhi vulkan`. Verify both backends for shared rendering changes.
- Use a unique session id and isolated `-StateRoot` under `Build/` when saved
  settings or shader/IBL caches could affect the experiment. Keep intended
  settings identical between comparison arms; do not erase the owner's state.

## Capture from a session

This example captures the GTAO Lab on DX12; change the content and backend to
match the task. The returned `image` and `metadata` paths are the evidence.

```powershell
$session = 'Scripts/Verification/GGLabSession.ps1'
$id = 'verify-' + [Guid]::NewGuid().ToString('N').Substring(0, 12)
$state = Join-Path (Get-Location) "Build/Sessions/$id/State"
$start = powershell -NoProfile -ExecutionPolicy Bypass -File $session start `
    -Session $id -Configuration Develop -Rhi dx12 -Lab gglab.lab.gtao `
    -WindowSize 1280x720 -NoDevTools -StateRoot $state | ConvertFrom-Json
if (-not $start.ok) { throw ($start | ConvertTo-Json -Depth 10) }
try {
    powershell -NoProfile -ExecutionPolicy Bypass -File $session status -Session $id
    $capture = powershell -NoProfile -ExecutionPolicy Bypass -File $session capture `
        -Session $id -SettleFrames 16 -Label overview | ConvertFrom-Json
    if (-not $capture.ok -or $capture.status -ne 'completed') {
        throw ($capture | ConvertTo-Json -Depth 10)
    }
    $capture | ConvertTo-Json -Depth 10
}
finally {
    powershell -NoProfile -ExecutionPolicy Bypass -File $session stop -Session $id
}
```

`start` accepting requests does not mean the content is ready. The default
`after-ready` capture waits for readiness and then settles submitted frames.
Use `status` to inspect pending gates. The custom-pipeline Labs
`gglab.lab.render_graph_compute` and `gglab.lab.coordinate_conformance` do not
open the `ibl` gate; use `-Timing next-frame` for their smoke captures and check
the actual content/logs before treating the image as valid.

## Inspect and finish

- A session command's exit code 0 and `ok: true` describe the control request.
  Also require capture `status: completed`, sequence `state: completed`, and
  the requested watch claims. Failed/cancelled work can still exit 0.
  With `-NoWait`, use `result -RequestId` for captures or `status` for sequences.
- Open the PNG with the harness's image-viewing tool. Inspect its JSON sidecar
  for content, camera, source, frame, time, resolution, readiness and temporal
  settings. Inspect `Build/Sessions/<id>/output.log` for assertions, shader
  failures, validation and lifetime/synchronization errors.
- Compare matched inputs and inspect difference images. A successful write,
  clean scan, shader compile or headless test alone does not establish visual
  correctness. A screenshot alone does not establish numerical or GPU timing
  correctness. Use the advanced references when those claims are needed.
- Stop every session you started, including after failure, and inspect the
  stop response's `exitCode`. Do not link while a target executable is running.
  Logs and default captures remain in `Build/Sessions/<id>/`; a session's
  default idle timeout is 900 seconds, not a cleanup strategy.
- Report the configuration, backend/content/view or path, inspected artifact
  paths, observed result and material gaps. Keep run-specific results out of
  this skill and its references.
