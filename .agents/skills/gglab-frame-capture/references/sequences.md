# Sequences, references and timing

Use camera paths registered by the active content. Coastal Atrium (`-Demo atrium`)
includes `SEQ_DollyDoorway` and `SEQ_StaticRailings`. With an existing `atrium`
session and `$session = 'Scripts/Verification/GGLabSession.ps1'`:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File $session sequence -Session atrium `
    -CameraPath SEQ_DollyDoorway -CaptureFrames '0,90,179' -Label dolly-taa
```

Frame 0 waits for readiness and resets history/jitter. Successful submission
advances the pose; an unsubmitted frame retries the same simulation instant.
Authored cuts permit history resets. Unexpected history/content/configuration
changes and readiness loss outside the event allowance below fail the sequence.

Requested images are exact captures labelled `<label>-f<frame>`. Capacity pressure
waits before advancing the pose/sample; supported late capacity deferral retries
with fresh actual-frame metadata. A diagnostic tap cannot silently move an exact
capture to a later path frame. Keep view changes out of an active sequence.
Choose one scene/diagnostic channel per replay and verify repeatability before
comparing matched frames from different runs.

## Completion and cancellation

One sequence runs at a time. The script waits by default; with `-NoWait`, poll
`status`. Require `sequence.state: completed` and the
[range publications](#published-range-artifacts) below. Failed/cancelled sequences
can return a successful control response. `finishing` waits for captures, watch
coverage, requested GPU profiles and the range publication receipt.

`sequence-cancel` returns `Cancelled | TooLate | Finished | NotFound`. Before the
range publication claim, cancellation prevents whole-sequence delivery. After the
claim, `TooLate` keeps the sequence active until its actual publication receipt:
Published completes; NotPublished/Indeterminate fails. Finished preserves terminal
status; NotFound means no sequence. Capture receipts and GPU/writer owners keep
draining after cancellation; a new sequence waits for them to drain.

## Parameter events

Check `status.capabilities.sequenceImmediateEvents`; Deferred events additionally
require `sequenceDeferredEvents` and `sequenceEventDomains`. `-Events`/`-EventsFile`
accept `{frame, set, expectEffects?}` objects. `set` maps discovered parameter ids
to values; JSON integers resolve against integer/enum/float descriptors, while
boolean, fractional-integer and vector/color validation stays strict.

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File $session sequence -Session atrium `
    -CameraPath SEQ_StaticRailings -CaptureFrames '9,10,95' `
    -Events '[{"frame":10,"set":{"view.exposure.ev100":17},"expectEffects":[]}]'
```

Ordinary sequences accept bound Immediate and Deferred lighting effects. Rebuild
and reference sequences with events reject. Omitted `expectEffects` infers domains;
`[]` means Immediate only; `["lighting"]` expects one bound Deferred lighting item,
optionally with Immediate items. Actual receipts must match explicit expectations.
An active-state NoChange starts no publication or readiness allowance.

The complete plan is descriptor-validated after readiness, before frame 0: a bad
later event prevents every earlier mutation. Duplicate frames reject; one frame
has one atomic batch. Limits are 128 events, 128 items/event, 1024 items total,
and one Deferred lighting item/event. Host assignments overlapping the same legacy
TAA/GTAO override field reject; unrelated legacy overrides remain usable.

Deferred transitions retain every sequence frame. Only this sequence owner's
applied outstanding receipt matching Runtime's current lighting request generation
can cover `ibl` Pending. Expected domains alone grant no exception. Failed gates,
unrelated requests and other gates still fail; `environment` means external asset
loading and is not covered. Temporal-continuity checks remain active outside cuts.

Atmosphere Range registers the 240-frame `SEQ_AtmosphereRange_Static` path:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File $session sequence -Session range `
    -CameraPath SEQ_AtmosphereRange_Static -CaptureFrames '9,10,80' `
    -Events '[{"frame":10,"set":{"environment.profile":1},"expectEffects":["lighting"]}]'
```

Events apply before building their exact frame. Abort/admission retries retain
the batch/revision without invoking effects twice. Receipt semantics are defined
in [Session parameters](session-parameters.md#read-effect-receipts).
`sequence.events[]` exposes frame/state/batch/failure snapshots; AppRuntime consumes
internal results. `parameter-result` cannot claim event batches. Consume retained
external results, including terminal ones, before starting an event sequence.

Unexecuted terminal events report `EffectNotReached` without invented stamps.
Cancel/content invalidation closes pending receipts while retaining reached stamps.
The last submitted frame closes unresolved items as EffectNotReached and fails
the sequence; rendering during the finishing tail cannot supply in-range consumption.
Superseded/failed effects fail explicitly. Committed values persist after completion
or cancellation, subject to normal content lifetime; there is no automatic rollback.

Use `Tests/Verification/RunVerification.ps1 -Suite sequence-parameter-events` for Demo/Lab Immediate
timelines, or `-Suite sequence-deferred-events` for texture IBL/Physical Sky timelines,
four stamps, temporal consumers and last-frame non-consumption on both RHIs.

## Published range artifacts

Every completed production sequence publishes required `gglab.parameter-frames` v1,
even without capture frames or GPU timing. Require
`sequence.parameterPublication.outcome: Published`. `parameterFrameCount` counts
all submitted samples, including uncaptured frames and every reference sample.
Its `frames` are in submission order with `sequenceFrame`, `referenceSample` and
frozen [parameters](session-jobs.md#frame-provenance).

With `-GpuTiming`, also require `sequence.timingPublication.outcome: Published`.
Both products share one publication claim, watch coverage and v3 bundle with
`consistency: sequence-range`, identity `<run>-s<sequence>-range`, and no SameFrame
job/attempt or single-frame provenance/submission fields. Join timing and parameters
by `(sequenceFrame, referenceSample, frameSerial)`, since timing results may arrive
out of order. Legacy v2 timing-only bundles retain their `-timing` identity.
Failed/cancelled ranges do not publish a complete range product; published frame
bundles remain independently valid.

Discover files from the manifest, then validate their contracts:

```powershell
$publication = $response.sequence.parameterPublication
$manifest = Get-Content -LiteralPath $publication.manifest -Raw | ConvertFrom-Json
$product = @($manifest.products | Where-Object schema -eq 'gglab.parameter-frames')[0]
$parametersFile = Join-Path (Split-Path -Parent $publication.manifest) $product.metadata
python Scripts/Verification/ValidateCaptureBundles.py $publication.manifest
python Scripts/Verification/ValidateParameterFrames.py $parametersFile
```

## Supersampled reference

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File $session sequence -Session atrium `
    -CameraPath SEQ_StaticRailings -CaptureFrames 0 -ReferenceSamples 256 -Label static-ref
```

`-ReferenceSamples` accepts 1-4096 renders per path frame, holding pose/time,
disabling temporal AA/GTAO, averaging jittered HDR samples, then post-processing.
Transparent geometry participates. `-ReferenceTextureLodBias` defaults to 0;
`-0.5 * log2(n)` selects a subpixel texture footprint for n samples. Compare n
and 2n for convergence. References accept scene color or `gtao-final-ao`, and
reject Temporal AA/GTAO overrides and parameter events.

Every path frame costs n renders even if only one image is requested. Cancel
after the needed images only when complete-range evidence is unnecessary;
cancellation does not produce a completed parameter/timing range. Metadata records
`sequence.referenceSamples` and reference jitter/sample state.

## Temporal AA and GTAO evaluation

`-TemporalAA 'name=value,...'` and `-GTAO 'name=value,...'` set overrides from frame 0.
Omitted values inherit content settings. Use the
[control parser](../../../../Sources/WinApp/Application/Control/ApplicationControlProtocol.cpp)
for names/ranges and saved metadata for effective values:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File $session sequence -Session atrium `
    -CameraPath SEQ_StaticRailings -CaptureFrames 95 `
    -TemporalAA 'neighborhoodClampExpansion=1,historyFilter=bilinear' -Label static-clamp
```

For GTAO, `temporal=true,temporalMaxSamples=16` is one useful comparison. Match
content, camera and frame state between production and reference replays.

## GPU timing and PIX

`-GpuTiming` collects frame/scope sample counts, mean, median, p90, min/max ms.
It includes frame 0, cuts and every reference sample without a warmup skip.
`gpuTiming.frames == frameCount * max(1, referenceSamples)`; scopes count only
renders that recorded them. Missing/invalid profiles fail. Failed/cancelled partial
timing is not a complete-run measurement.

The published `gglab.gpu-timing` v1 product is authoritative; the response summary
is a convenience. Discover it through the range manifest above. `frame.milliseconds`
follows `coverage.samples` arrival order; scope `sampleIndices` indexes that array
and its `milliseconds` follows those indices. Use render serials for submission
order. `stablePassSet` reports whether every sample recorded the same pass identities.

Scopes use `kind: pass|other`. Pass `{name, occurrence}` matches graph declaration
identity, including culled declarations; equal names with different occurrences
are distinct. Other scopes have null occurrence and cannot join a graph pass.
Identity is local to one graph, not stable across versions. Single-frame graphs
keep `execution.gpuTiming` unavailable.

```powershell
$product = @($manifest.products | Where-Object schema -eq 'gglab.gpu-timing')[0]
$timingFile = Join-Path (Split-Path -Parent $publication.manifest) $product.metadata
python Scripts/Verification/ValidateGpuTiming.py $timingFile
powershell -NoProfile -ExecutionPolicy Bypass -File Tests/Verification/RunVerification.ps1 `
    -Suite pass-profile-identity -Configuration Release -Rhi dx12,vulkan
```

For ordinary performance runs, use Release, validation off, no DevTools and
observation tools off. Compare within a backend/session and repeat small differences.
Develop defaults to standard validation; `start -Validation off|standard|gpu`
selects validation. Vulkan rejects `gpu`; one-shot uses `--validation`.

DX12 Debug/Develop integrate PIX by default with its capturer HUD hidden. Release
does not automatically load the capturer. Explicit PIX-enabled Release builds use
`/p:GGLAB_ENABLE_PIX=1` (`0` disables integration), without changing optimization,
CRT or validation. PIX can inject into normal Release externally; use that normal
Release build for PIX Timing Capture profiling.
