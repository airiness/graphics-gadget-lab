# Session Lab parameters (additive protocol v4)

Use the script and application from the same revision. Check
`status.capabilities.labParameterControl` before using these commands against an
older v4 application. The extension adds commands and status fields; existing
capture/result/sequence fields and their v4 meanings are unchanged.

This path controls only the active Lab's declared **Immediate** parameters.
Host `view.*` / `environment.*`, named profiles, deferred lighting publication,
rebuild transactions, sequence events and parameter metadata in saved frame
artifacts are not implemented. `set` rejects during an active sequence; a new
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
1–128 items with distinct ids (1–128 UTF-8 bytes each). Unknown ids, mismatched
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
owned `values`, `items`, failure and terminal/success indicators. Each effect item
reports its declared class, actual revision/domain/generation identity, state,
requested/applied/published/firstConsumed stamps and failure. Immediate
`published == applied`; only the exact successfully submitted frame state can
supply `firstConsumed`. `NoChange` records requested/applied confirmation but no
publication/consumption and introduces no revision or handler/bake request.
Superseded, failed and unreached effects are terminal without successful
consumption. An apply handler that actually starts a lighting transition fails
the receipt rather than claiming Immediate qualification.

Timeout and polling transport failure preserve `batchId` without resubmitting,
cancelling or inventing an outcome. A lost consuming response may already have
released the slot. If admission's response is lost, `admissionUnknown` is true;
inspect `status.parameterControl.batch` and its values before deciding how to
recover. It is evidence for inspection, not an idempotency key. Do not blindly
resubmit. File/transport failures still print one JSON error result.

## Verify on native backends

`Tests/Verification/RunVerification.ps1 -Suite session-parameters` runs the hidden
GTAO Lab on both RHIs by default and accepts Debug/Develop/Release. It exercises
atomic rejection, NoWait/KeepResult/wait/explicit consumption, NoChange, identity
and sequence exclusion; captures final-AO diagnostics for two radii, checks the
actual built settings, and measures sampled pixel changes. Native standard
validation is requested. Inspect the capture PNGs and logs as well. It is scoped
GTAO parameter qualification, not all-Lab/Deferred/event, cross-backend identity,
performance or real driver-fault proof.
