# Session capture jobs (protocol v3)

Use `Scripts/Verification/GGLabSession.ps1` and the executable from the same
revision. The pipe accepts only protocol **3**; retired v1/v2 and unknown versions
are rejected, not translated. Every request/response has `protocol`, unsigned
`id` (transport correlation), and the response has boolean `ok`.

## Submit and wait

`capture`, `graph` and `export` are direct builders of one typed `CapturePlan`.
All products are required and belong to the same submitted frame. The wire
submission always returns admission immediately; waiting is a client operation.

```powershell
$session = 'Scripts/Verification/GGLabSession.ps1'
$products = '[{"kind":"image","source":"scene"},{"kind":"resource","targets":[{"resource":"TAA.ResolvedSceneColor"}]},{"kind":"graph"}]'
powershell -NoProfile -ExecutionPolicy Bypass -File $session capture `
    -Session check -Products $products -Label observed
```

Use `-ProductsFile` to avoid shell quoting. Products are ordered objects:

| `kind` | Fields |
| --- | --- |
| `image` | Required `source`: `scene` or `composited`. |
| `diagnostic` | Required `diagnosticTap`: a supported diagnostic preview tap. |
| `graph` | Optional boolean `summary` (default false). |
| `resource` | Required non-empty `targets`: [resource addresses](graph-and-resources.md). |

An explicit `products` array cannot be combined with convenience `source` or
`diagnosticTap`. `graph -Summary` builds one graph product; `export -IncludeGraph`
builds a resource product followed by its required full graph. At most one graph
and one resource product are supported. A resource pair cannot use a summary
graph. One frame can select one distinct diagnostic tap; incompatible taps cannot
produce a successful SameFrame bundle. Optional/mixed-frame products are not
supported.

Common wire fields are `timing` (`after-ready` by default), `settleFrames`
(default 8, range 0–10000), `requiredContentId`, `view`, absolute
`outputDirectory`, `label` and `note`. There is no wire `wait` or `requestId`.

## Admission, query and explicit consumption

Admission returns `admission: Accepted`, `reason: None` and a non-zero `jobId`.
`Busy`/`Rejected` returns `ok: false`, `jobId: null`, a structured `reason` and an
`error`; it allocates neither identity nor receipt. Do not retry a rejected plan
unchanged. Busy is backpressure, not a terminal job failure. Accepted plans wait
for temporary payload/writer capacity; impossible or unsupported work fails explicitly.

`status` advertises `capabilities` and actual `capture.limits`. Defaults are 64
active jobs, 128 receipts, eight required products and 64 resource targets per
job. These are record counts, **not** total-process byte bounds or GPU payload
limits. Terminal receipts continue to reserve their original slot. There is no
silent expiry or eviction, secondary host result cache, or consumed-ID tombstone.

```powershell
$queued = powershell -NoProfile -ExecutionPolicy Bypass -File $session capture `
    -Session check -NoWait | ConvertFrom-Json
powershell -NoProfile -ExecutionPolicy Bypass -File $session result -Session check -JobId $queued.jobId
powershell -NoProfile -ExecutionPolicy Bypass -File $session result -Session check -JobId $queued.jobId -Consume
```

Wire query: `{"protocol":3,"id":7,"command":"result","jobId":1,"consume":false}`.
`consume` is optional and defaults to false. Pending work returns
`status: pending`, `consumed: false`, `stage`, `attemptCount`, nullable
`frameSerial`, `requiredProducts` and `finishedProducts`. Even `consume: true`
does not consume a pending job. Terminal query returns the immutable receipt;
only terminal `consume: true` releases it. Unknown or consumed IDs return
`ok: false`, `status: not-found`. IDs are process-local, not durable identities.

Waiting script commands explicitly consume the final receipt by default. Use
`-KeepResult` to retain it, then `result -Consume` when finished. `-NoWait` returns
admission only. A client timeout reports `jobId` without cancelling or consuming
pending work. Admission's transport timeout is at least 30 seconds, or the larger
configured `-TimeoutSeconds`; polling uses the remaining wait budget. This avoids
an unrelated fixed 30-second failure during cold instrumented shader startup.
Polling transport errors retain the accepted `jobId` without inventing a terminal
outcome or retrying submission. If the admission response itself is lost, the
client has no confirmed identity; do not blindly resubmit. Polling counts as
session activity; stored receipts do not.
Session termination cancels/drains work and ends receipt retention; artifacts
are not deleted. A lost response to a consume request is not replayable. The
client must tolerate an unknown/consumed result after transport failure.

## Interpret a terminal result

A terminal receipt has `jobId`, `status` (`completed`, `failed`, `cancelled`),
`publication` (`NotPublished`, `Published`, `PartiallyPublished`, `Indeterminate`),
`consumed`, nullable `bundleId`/`manifest`, and ordered `products`. Every product
has `index`, plan `kind` (also present before issue), `status`, `publication`,
`files`, `failure`, `failureCode` and nullable
`metadata` from the owning frame serializer. Product `kind` is authoritative;
paired graph/resource files can share underlying frame metadata, whose `source`
is not a substitute for required-product identity. Nested `metadata.requestId`
remains the existing frame-artifact identity, not a session `jobId`.
Top-level `frameSerial` and `watch`
are summaries from the first available product metadata; `failure` is the first
non-empty product failure. Product records remain authoritative.

Require both `status: completed` and `publication: Published`, then validate the
final `gglab.capture-bundle` v1 manifest and its required files. Individual file
existence is not job completion. Select images from `products[index].files`, not
legacy `image`/`metadata`/`graph`/`exports` response fields. For verification:

```powershell
Import-Module ./Scripts/Verification/VerificationSupport.psm1
$png = Get-VerificationCaptureFile $result '.png' 0
python Scripts/Verification/ValidateCaptureBundles.py $result.manifest
```

## Cancel delivery, not resource retirement

`capture-cancel -JobId <id>` sends `command: capture-cancel`. It returns `ok: true`
and `cancellation: Cancelled | TooLate | Finished | NotFound`, separately from the
job's terminal status/publication. Query and explicitly consume the real result.
`TooLate` does not rewrite a successful job as failed. `Cancelled` prevents job
publication but GPU payloads/writer reservations may still be draining after its
receipt is consumed; `status.capture` exposes those owners separately.

## Migration boundary

`capabilities.sequenceCaptureJobs` is currently false. Sequence commands retain
business progress/coverage fields, but do not expose legacy `captureRequestIds`
as if they were job IDs. `completedCaptures` counts successful image/resource
deliveries, not manifest-bearing jobs. Sequence, interactive UI and one-shot
capture internals still await migration and CAP-S5 adapter retirement. This
session cutover does not establish their intake/retention bounds or final-tree
parity gates.
