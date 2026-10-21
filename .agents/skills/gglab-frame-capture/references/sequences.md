# Sequences, references and timing

Use authored camera paths for temporal tests. Path ids and definitions belong to
the active content; inspect its registration rather than assuming another Lab's
path exists. Coastal Atrium (`-Demo atrium`) includes `SEQ_DollyDoorway` and
`SEQ_StaticRailings`.

With `$session = 'Scripts/Verification/GGLabSession.ps1'` and an existing session `atrium`:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File $session sequence -Session atrium `
    -CameraPath SEQ_DollyDoorway -CaptureFrames '0,90,179' -Label dolly-taa
```

Frame 0 waits for ready content and resets temporal history/jitter. Each submitted
frame advances one path frame; an unsubmitted frame retries the pose. Authored
cuts permit their history reset. Unexpected history loss, readiness loss or
content/configuration changes fail the sequence.

Requested frames are exact next-frame captures labelled `<label>-f<frame>`.
Capacity pressure waits before advancing the pose/sample. Admission Busy consumes
no identity or receipt; a supported late capacity deferral retries the same
simulation instant with fresh actual-frame metadata. Other frame failures remain
explicit; another diagnostic tap cannot silently move an exact capture to a
later path frame. Avoid submitting view changes during a sequence.

Each due frame submits one required-product job and publishes its final bundle
manifest after all required products. AppRuntime automatically consumes sequence
receipts. Status exposes accepted/finished/pending job counts; `completedCaptures`
keeps image/resource delivery counting, so a paired graph adds no delivery.
These jobs share session admission limits and do not accumulate terminal receipts
or legacy output IDs for the full replay. Published files remain on disk.

One sequence runs at a time. `sequence` waits by default; with `-NoWait`, poll
`status`. Require `sequence.state: completed`; failed/cancelled sequences also
return a successful control response. `finishing` waits for writes, watch
coverage and requested GPU profiles. `sequence-cancel` stops a run and cancels
whole-job delivery. Already claimed publication can finish later. Pending job
receipts and GPU/writer owners keep draining after cancellation; a new sequence
waits until prior sequence receipts are consumed.

Choose one evidence channel per replay: scene, or
`-Source diagnostic -DiagnosticTap temporal-history-weight` (for example).
Replay per channel/configuration. Confirm deterministic content by replaying the
same path twice and checking the images and metadata before relying on paired
frames from different runs.

## Supersampled reference

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File $session sequence -Session atrium `
    -CameraPath SEQ_StaticRailings -CaptureFrames 0 -ReferenceSamples 256 -Label static-ref
```

`-ReferenceSamples` accepts 1-4096 samples per path frame. It holds pose/time,
disables Temporal AA, averages jittered HDR samples and then post-processes the
image. Transparent geometry participates in the reference accumulation. The
optional `-ReferenceTextureLodBias` defaults to 0 (whole-pixel texture filtering);
`-0.5 * log2(n)` selects a subpixel footprint for n samples. Compare n and 2n to
assess convergence instead of assuming a sample count is sufficient.

Every path frame costs n renders even if only one frame is captured. After the
needed captures finish, cancel if the remaining path is unnecessary. Metadata
records `sequence.referenceSamples` and the reference jitter/sample state.
References accept scene color or the `gtao-final-ao` diagnostic tap, and reject
Temporal AA/GTAO overrides.

## Temporal AA and GTAO evaluation

`-TemporalAA 'name=value,...'` and `-GTAO 'name=value,...'` apply overrides for the
whole replay from frame 0. Omitted values retain the content's settings, not
necessarily built-in defaults. Unknown/out-of-range values fail. Read the
[control parser](../../../../Sources/WinApp/Application/Control/ApplicationControlProtocol.cpp)
for accepted settings/ranges, and inspect effective values in capture metadata.

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File $session sequence -Session atrium `
    -CameraPath SEQ_StaticRailings -CaptureFrames 95 `
    -TemporalAA 'neighborhoodClampExpansion=1,historyFilter=bilinear' -Label static-clamp
```

Other useful Temporal AA controls include `enabled`, `maxHistoryFeedback`,
`historyAccumulation`, `historyRectification`, `varianceClipGamma`,
`currentFilter`, `motionSelection`, `resolutionPreset` and `textureLodBiasOffset`.
GTAO supports controls such as `temporal=true,temporalMaxSamples=16`. A reference
run and a production replay must use matching content, camera and frame state.

## GPU timing and PIX

Add `-GpuTiming` to a sequence to collect frame/scope sample count, mean, median,
p90, min and max in milliseconds. Timing includes frame 0, cuts and every
reference sample; there is no warmup skip. After completion, `gpuTiming.frames`
equals `frameCount * max(1, referenceSamples)`; a scope counts only renders that
recorded it. Missing/invalid profiles fail explicitly. Partial timing from failed
or cancelled sequences is not a complete-run measurement.

Use Release, validation off, no DevTools and observation tools off for ordinary
performance runs. Compare configurations within the same backend/session where
possible; repeat small differences to assess noise. Graph exports cannot provide
identity-preserving per-pass GPU timing; use sequence scope statistics for the
claims they actually support.

Develop uses standard validation; `start -Validation gpu` opts into DX12 GPU-based
validation, while `-Validation off` disables it. One-shot launches use
`--validation off|standard|gpu`. Vulkan rejects `gpu`.

DX12 Debug/Develop integrate PIX by default and hide its capturer HUD. Release
does not automatically load the GPU capturer. For an explicitly requested
PIX-enabled Release build, pass `/p:GGLAB_ENABLE_PIX=1` to MSBuild (`0` disables
integration in any configuration). This does not change optimization, CRT or
validation. PIX can inject externally into a normal Release executable. For PIX
Timing Capture profiling, use normal Release without automatic capturer loading.
