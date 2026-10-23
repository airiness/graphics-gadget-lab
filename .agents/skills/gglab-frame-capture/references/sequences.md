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
coverage, requested GPU profiles and the requested timing publication receipt.
`sequence-cancel` returns `cancellation: Cancelled | TooLate | Finished | NotFound`.
Before eligibility or a timing publication claim it cancels whole-sequence
delivery. After that claim, `TooLate` keeps the sequence active until its real
receipt: `Published` completes it; `NotPublished` or `Indeterminate` fails it.
`Finished` preserves terminal status; `NotFound` has no sequence. Pending capture
receipts and GPU/writer owners keep draining after cancellation; a new sequence
waits until prior receipts and the timing writer have drained.

Choose one evidence channel per replay: scene, or
`-Source diagnostic -DiagnosticTap temporal-history-weight` (for example).
Replay per channel/configuration. Confirm deterministic content by replaying the
same path twice and checking the images and metadata before relying on paired
frames from different runs.

## Parameter events

Check `status.capabilities.sequenceImmediateEvents`. Pass `-Events` or `-EventsFile`
with `{frame, set, expectEffects?}` objects. `set` maps discovered parameter ids to
values; JSON integers resolve against the descriptor's integer/enum/float storage.
Booleans, fractional integers and vector/color shapes remain strict. For example:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File $session sequence -Session atrium `
    -CameraPath SEQ_StaticRailings -CaptureFrames '9,10,95' `
    -Events '[{"frame":10,"set":{"view.exposure.ev100":17},"expectEffects":[]}]'
```

Ordinary sequences support bound Immediate and Deferred lighting parameters.
Check `sequenceDeferredEvents` and `sequenceEventDomains` before using Deferred
events. Rebuild parameters and reference sequences with events reject. Omitted
`expectEffects` infers the source domains; `[]` expects Immediate parameters only,
and `["lighting"]` expects a bound Deferred lighting item, optionally mixed with
Immediate items. NoChange is permitted for that bound domain and starts no
publication or readiness allowance. Unsupported or mismatched domains reject.
The complete plan is descriptor-validated after target readiness, before frame 0.
A bad later event prevents all earlier event mutation. Duplicate frames reject:
one frame has one atomic batch. Limits are 128 events, 128 items per event and
1024 items total. A Host assignment overlapping the same legacy TAA/GTAO override field rejects;
unrelated legacy overrides remain usable. Each event admits at most one Deferred
lighting item. The actual receipts must also match any explicit expected domains.

Deferred transitions continue rendering and recording every sequence frame. Only
an applied outstanding receipt owned by this sequence, matching the Runtime's
current lighting request generation, can cover `ibl` Pending. Failed gates,
unrelated requests and other gates still fail. In particular, `environment` is
external asset loading and is not owned by these synchronous profile/sample-count
mutations. An expected domain alone grants no exception. Temporal continuity
checks remain active outside authored camera cuts.

The Atmosphere Range Lab registers `SEQ_AtmosphereRange_Static` (240 frames) for
profile transitions between its authored baseline and oblique fixtures:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File $session sequence -Session range `
    -CameraPath SEQ_AtmosphereRange_Static -CaptureFrames '9,10,80' `
    -Events '[{"frame":10,"set":{"environment.profile":1},"expectEffects":["lighting"]}]'
```

Events apply before building their exact sequence frame. Abort/admission retries
retain the same batch/revision without invoking effects twice. Only matching
successful submission supplies `firstConsumed`. Session owns live receipts;
`sequence.events[]` reports snapshots and AppRuntime consumes internal results.
`parameter-result` cannot claim or consume an event batch. An event sequence also
rejects retained external results, including terminal ones; consume them first.

Each report includes `frame`, `state`, `batch` and `failure`. Unexecuted events in a
terminal sequence report `EffectNotReached` without invented batches/stamps.
Cancel/content invalidation closes outstanding receipts and retains reached stamps.
The last submitted frame closes every unresolved item as `EffectNotReached` and
fails that sequence; later rendering while captures/timing drain cannot supply
in-range consumption. Superseded/failed effects also fail explicitly. Independent
receipts may share a pending target without requesting another bake; an unrelated
later parameter revision does not supersede them. A different committed assignment
to the same parameter does supersede the older unconsumed event.
Committed values remain session settings after completion/cancel; normal content
lifetime clears them, with no automatic sequence rollback. Full persisted parameter
metadata remains separate work.

The hidden `sequence-parameter-events` suite verifies Demo/Lab frame-ten exposure
and GTAO changes. `sequence-deferred-events` covers texture IBL and Physical Sky
profile timelines, captured old/active generations, four stamps, real temporal
consumers and last-frame non-consumption on both RHIs. AppRuntime fault tests
exercise abort/retry, whole-plan rejection, owner isolation, shared-target waits,
supersession, unrelated readiness changes, cancellation and content invalidation.

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
possible; repeat small differences to assess noise.

Each scope summary carries `kind: pass|other` and `occurrence`. A pass uses the
same `{name, occurrence}` as graph declarations: occurrence is assigned once in
declaration order and includes culled declarations. Equal names with different
occurrences remain separate series. An `other` scope has null occurrence and is
never joined to a graph pass. Identity is local to one graph, not a stable
semantic identity across versions. A scope counts only frames that recorded it.

The published `gglab.gpu-timing` v1 product is the measurement authority; the
response summary remains a convenience. Require `sequence.state: completed` and
`sequence.timingPublication.outcome: Published`, read its `manifest`, then discover
the timing file from the product with `schema: gglab.gpu-timing`. The bundle is
v2 with `consistency: sequence-range` and identity `<run>-s<sequence>-timing`. It
has no SameFrame job/attempt or single-frame provenance/submission fields.

The product records the sequence range, context, every render serial and
frame/reference index, first-frame/cut flags and per-sample profiling, DevTools
and observation conditions. `frame.milliseconds` follows `coverage.samples`
order. Each scope's `sampleIndices` indexes that array; its `milliseconds` follows
those indices. Sample order is result arrival order, so use the recorded serials
for submission order. `stablePassSet` reports whether every sample recorded the
same pass identities. No cold/cut/reference samples are silently filtered.
Validate both discovery and sample/statistics semantics:

```powershell
$publication = $response.sequence.timingPublication
$manifest = Get-Content -LiteralPath $publication.manifest -Raw | ConvertFrom-Json
$product = @($manifest.products | Where-Object schema -eq 'gglab.gpu-timing')[0]
$timingFile = Join-Path (Split-Path -Parent $publication.manifest) $product.metadata
python Scripts/Verification/ValidateCaptureBundles.py $publication.manifest
python Scripts/Verification/ValidateGpuTiming.py $timingFile
```

Single-frame graph exports keep `execution.gpuTiming` unavailable. Check graph
joins, sample coverage, published-versus-response statistics and backend identity
parity with the hidden regression:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File Tests/Verification/RunVerification.ps1 `
    -Suite pass-profile-identity -Configuration Release -Rhi dx12,vulkan
```

Develop uses standard validation; `start -Validation gpu` opts into DX12 GPU-based
validation, while `-Validation off` disables it. One-shot launches use
`--validation off|standard|gpu`. Vulkan rejects `gpu`.

DX12 Debug/Develop integrate PIX by default and hide its capturer HUD. Release
does not automatically load the GPU capturer. For an explicitly requested
PIX-enabled Release build, pass `/p:GGLAB_ENABLE_PIX=1` to MSBuild (`0` disables
integration in any configuration). This does not change optimization, CRT or
validation. PIX can inject externally into a normal Release executable. For PIX
Timing Capture profiling, use normal Release without automatic capturer loading.
