# Session Lab parameters (additive protocol v4)

Use the script and application from the same revision. Check
`status.capabilities.labParameterControl` before using these commands against an
older v4 application. The extension adds commands and status fields; existing
capture/result/sequence fields and their v4 meanings are unchanged.

This path controls the active Lab's Immediate parameters and **one explicitly
bound Deferred lighting parameter per batch**, optionally mixed with Immediate
items. Current bindings are `gtao.ibl.sample_count` and
`atmosphere_range.ibl.sample_count` (uint, 1–4096). They change IBL specular sample
count through the owning Runtime control, preserving generation attribution.
The GTAO fixture covers texture-IBL cache restoration; Atmosphere Range covers
Physical Sky, whose current producer deliberately skips texture caches.

Host `view.*` / `environment.*`, named profiles, arbitrary authored sun/atmosphere
parameter bindings, rebuild transactions, sequence events and parameter metadata
in saved frame artifacts are not implemented. `set` rejects during an active sequence; a new
sequence rejects while a parameter batch is pending. Do not use external `set`
as a substitute for scheduled events.

## Discover and set

```powershell
$script = 'Scripts/Verification/GGLabSession.ps1'
powershell -NoProfile -ExecutionPolicy Bypass -File $script parameters -Session check
# Prefer a UTF-8 file to avoid shell quoting; preserve the outer array for one item.
$changesFile = Join-Path (Get-Location) 'Build/radius-changes.json'
[IO.File]::WriteAllText($changesFile,
    '[{"id":"gtao.radius","type":"float","value":2.0}]',
    (New-Object Text.UTF8Encoding($false)))
powershell -NoProfile -ExecutionPolicy Bypass -File $script set -Session check `
    -ChangesFile $changesFile -WaitConsumed
```

`parameters` returns the active `contentId`, current `revision`, and descriptors
with `id`, `source`, display `name`/`group`, `type`, `impact`, `declaredClass`,
`editPolicy`, `default`, `current`, nullable `min`/`max`, `enumItems` and
`setSupported`. The latter describes the supported effect class; admission can
still be Busy or require a ready Lab. No active Lab returns `NoParameterSource`.

Wire set: `{"protocol":4,"id":7,"command":"set","changes":[{"id":"gtao.radius","type":"float","value":2.0}]}`.
Every item takes exactly `id`, `type`, `value`. Types must match the descriptor:

| Type | JSON value |
| --- | --- |
| `bool` | Boolean; no numeric or string coercion. |
| `int`, `enum` | Signed 32-bit integer; enum values must be declared item integers. |
| `uint` | Unsigned 32-bit integer. |
| `float` | Finite number representable as a 32-bit float. |
| `vector3` | Three finite float components. |
| `color` | Four finite float components, RGBA. |

Integer types reject fractional/exponent JSON numbers even when mathematically
integral. Float inputs use float storage, without range clamping. A batch has
1–128 items with distinct ids (1–128 UTF-8 bytes each). Capability fields additionally
advertise `effectClasses`, `deferredDomains: ["lighting"]` and
`maxLightingItemsPerBatch: 1`. Unbound Deferred declarations reject. Unknown ids, mismatched
types, invalid enums/ranges, unsupported effects and any invalid item reject the
whole batch without changing live values or revision. String parsing for Debug
startup `-LabParameters` is a separate path.

## Wait, query and consume

Wire submission immediately returns `admission: Accepted`, `batchId`,
`consumed: false` and a `Queued` batch. No revision or receipt stamps are invented
before the actual Ready frame. There is one Lab-owned accepted batch/result
slot; terminal results retain it until explicit consumption. Rejection allocates
no batch id. IDs are process-local and **separate from capture `jobId`** and
transport correlation `id`.

The script waits for application by default. `-NoWait` returns admission only;
`-WaitConsumed` waits for every effect to become terminal (including failure or
supersession), and cannot be combined with `-NoWait`. Waiting queries explicitly
consume terminal results unless `-KeepResult`. If default waiting returns an
applied but nonterminal batch, it remains owned and must be queried/consumed
later. A command's `ok` and exit 0 describe the control operation; require
`batch.terminal` and `batch.succeeded: true` for successful effects.

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File $script set -Session check `
    -ChangesFile $changesFile -NoWait
powershell -NoProfile -ExecutionPolicy Bypass -File $script parameter-result `
    -Session check -BatchId 1
powershell -NoProfile -ExecutionPolicy Bypass -File $script parameter-result `
    -Session check -BatchId 1 -Consume
```

Wire query: `{"protocol":4,"id":8,"command":"parameter-result","batchId":1,"consume":false}`.
Pending consumption never releases the slot. Wrong, consumed or unavailable
owner ids return `status: not-found`. `status.parameterControl` reports current
availability/revision and borrows the same batch. It has no second result cache
or unbounded history. Retained batches preserve their owned admission values,
even if application fails or a Lab session is replaced. The active Lab runtime
locator must still be available to query its slot; switching to Demo content
does not expose the inactive Lab owner. Process termination ends retention.

`batch` includes source/content, state (`Queued`, `Applying`, `Finished`,
`Failed`), revision (null until application), fixed supported impact `Immediate`,
owned `values`, `items`, failure and terminal/success indicators. The impact
means no Lab rebuild; it is independent of the per-item effect class. Each effect item
reports its declared class, actual revision/domain/generation identity, state,
requested/applied/published/firstConsumed stamps and failure. Immediate
`published == applied`; only the exact successfully submitted frame state can
supply `firstConsumed`. `NoChange` records requested/applied confirmation but no
publication/consumption and introduces no revision or handler/bake request.
Superseded, failed and unreached effects are terminal without successful
consumption. The bound handler must report one actual Runtime generation advance,
with the previous and resulting generation matching the apply boundary. Unsupported
or unproven attribution fails; it is never predicted from the descriptor or the
`ibl` readiness gate. A mixed batch retains per-item classes and stamps.

The Runtime frame builder freezes the published lighting generation in its
`RenderScene`. That immutable scene state, followed by successful submission,
establishes Deferred consumption. Publication observed in an aborted build stays
recorded, while retry alone can supply firstConsumed. New requests/values can
supersede pending states; a post-build request cannot relabel an already built
frame. A terminal failed batch closes unresolved siblings without erasing their
existing evidence. Accepted application/effect failure does not roll back live
values or pretend already-started GPU work was cancelled.

NoChange lighting causes no new request; an all-NoChange batch invokes no handler.
The fixtures change global bake quality only while entered and restore the owned
override on exit. If no sample override was used, they do not restore unrelated
quality edits. Declared Immediate behavior contradicting an actual lighting
request continues to fail as EffectMismatch.

Timeout and polling transport failure preserve `batchId` without resubmitting,
cancelling or inventing an outcome. A lost consuming response may already have
released the slot. If admission's response is lost, `admissionUnknown` is true;
inspect `status.parameterControl.batch` and its values before deciding how to
recover. It is evidence for inspection, not an idempotency key. Do not blindly
resubmit. File/transport failures still print one JSON error result.

## Verify on native backends

`Tests/Verification/RunVerification.ps1 -Suite session-parameters` runs the hidden
GTAO and Atmosphere Range Labs on both RHIs by default and accepts
Debug/Develop/Release. For a focused rerun, invoke
`TestSessionParameters.ps1 -Scope lighting` directly with typed `-Rhi` arrays;
the default scope includes both Immediate and Deferred coverage. Immediate
regression covers atomic rejection, result
ownership, NoChange, identity and sequence exclusion, plus final-AO diagnostics
for two radii. Deferred regression changes sample count in three arms, verifies
mixed classes and four stamps, matches the actual captured generation/config,
and checks publication logs: a restored texture configuration must publish a
new generation with four cached stages and no GPU build stages; Physical Sky
must use uncached GPU bakes. Native standard validation is requested. Inspect
the capture PNGs and logs as well. These are scoped fixture parameter contracts,
not arbitrary sun/profile, scheduled-event, cross-backend identity, performance
or real driver-fault proof.
