# Session capture jobs and artifacts (protocol v4)

Use the script and executable from the same revision. The pipe accepts only
protocol 4. Requests/responses carry unsigned transport correlation `id` and
responses carry `ok`. Parameter control uses separate
[`batchId` / `parameter-result`](session-parameters.md) handles.

## Submit required products

`capture`, `graph` and `export` build one plan of required products from the same
submitted frame. Wire admission returns immediately; waiting is a client operation.

```powershell
$session = 'Scripts/Verification/GGLabSession.ps1'
$products = '[{"kind":"image","source":"scene"},{"kind":"resource","targets":[{"resource":"TAA.ResolvedSceneColor"}]},{"kind":"graph"}]'
powershell -NoProfile -ExecutionPolicy Bypass -File $session capture `
    -Session check -Products $products -Label observed
```

Use `-ProductsFile` to avoid shell quoting. Ordered product objects accept:

| `kind` | Fields |
| --- | --- |
| `image` | Required `source`: `scene` or `composited`. |
| `diagnostic` | Required `diagnosticTap`. |
| `graph` | Optional boolean `summary`, default false. |
| `resource` | Non-empty `targets`: [resource addresses](graph-and-resources.md). |

Explicit products cannot accompany convenience `source`/`diagnosticTap` fields.
At most one graph and one resource product are supported. `export -IncludeGraph`
adds a required full graph; summary graphs cannot pair with resources. A frame
supports one distinct diagnostic tap. Optional or mixed-frame products reject.
Common wire fields are `timing` (default `after-ready`), `settleFrames` (default 8,
0-10000), `requiredContentId`, `view`, absolute `outputDirectory`, `label`, `note`.
There is no wire `wait` or `requestId`.

## Admit, query and consume

Accepted admission returns `reason: None` and a nonzero `jobId`. Busy/Rejected
returns `ok: false`, null `jobId` and a structured reason/error, allocating no
identity or receipt. Busy is temporary backpressure; do not retry an unchanged
rejected or oversized plan. Accepted jobs wait for temporary payload/writer capacity.

Read `status.capabilities` and actual `capture.limits`. Defaults are 64 active
jobs, 128 receipts, eight products and 64 resource targets per job. These are
record counts, not GPU payload or total-process byte bounds. Terminal receipts
retain their slots until consumed, without expiry or eviction.

```powershell
$queued = powershell -NoProfile -ExecutionPolicy Bypass -File $session capture `
    -Session check -NoWait | ConvertFrom-Json
if (-not $queued.ok) { throw ($queued | ConvertTo-Json -Depth 10) }
powershell -NoProfile -ExecutionPolicy Bypass -File $session result `
    -Session check -JobId $queued.jobId
powershell -NoProfile -ExecutionPolicy Bypass -File $session result `
    -Session check -JobId $queued.jobId -Consume
```

Pending queries expose stage, attempt count and product progress; even `-Consume`
cannot release pending work. Unknown/consumed ids return `ok: false`,
`status: not-found`. Waiting commands consume terminal receipts unless
`-KeepResult`; `-NoWait` returns admission only. Only jobs admitted through this
session pipe are accessible through its `result` and `capture-cancel` commands.

Client timeout/polling errors preserve accepted `jobId` without cancellation or
resubmission. Admission transport timeout is at least 30 seconds or the larger
`-TimeoutSeconds`; polling uses the remaining wait budget. Lost admission has no
confirmed identity: do not blindly resubmit. A lost consuming response can mean
already-consumed work. Polling counts as activity; stored receipts do not.
Termination drains/cancels work and ends retention, leaving artifacts on disk.
Transport failures still print one JSON error result and exit 1.

### Run-checked requests

A session name can be reused by a restarted process, whose job and batch ids may
repeat. `start` reports the new process's `runIdentity`, and `status` reports it as
`environment.process.runIdentity`. Pass `-ExpectedRunIdentity <id>` to make `set`,
`parameter-result`, `capture`, `graph`, `export`, `watch`, `result`,
`capture-cancel`, `sequence`, `sequence-cancel` and `stop` reach only that run. A
different run answers `ok: false` with `errorCode: ExpectedRunChanged` and its own
`runIdentity` before looking up any handle, so nothing is submitted, consumed,
cancelled or stopped. Treat that as a lost session rather than retrying without the
check. `status.capabilities.runIdentityChecks` lists the checked commands.

## Interpret results and artifacts

Require `status: completed` and `publication: Published`, then validate the final
`manifest` and required files. Failed/cancelled work can return a successful
control response. A PNG or JSON file alone does not establish publication.

Terminal receipts retain `jobId`, status/publication, `bundleId`, `manifest` and
ordered `products`. Each product has `index`, `kind`, status/publication, `files`,
`failure`, `failureCode` and nullable frame `metadata`. Select files by product
kind/index, not flattened response fields or shared metadata's `source`.
Top-level frame/watch/failure fields are summaries; product records are authoritative.
Nested artifact `metadata.requestId` is distinct from the session `jobId`.

```powershell
Import-Module ./Scripts/Verification/VerificationSupport.psm1
$png = Get-VerificationCaptureFile $result '.png' 0
python Scripts/Verification/ValidateCaptureBundles.py $result.manifest
```

| Artifact | Current writer / compatibility |
| --- | --- |
| `gglab.capture-bundle` | v3; readers retain v1/v2. |
| Image JSON sidecar | v1, with nested `parameters` v1. |
| `gglab.render-graph` | v6; readers retain v1-v5 without parameter provenance. |
| `gglab.resource-export` | v3; readers retain v1/v2 without parameter provenance. |
| `gglab.parameter-frames`, `gglab.gpu-timing` | v1 sequence-range products; see [Sequences](sequences.md#published-range-artifacts). |

The manifest's `environment`, also exposed by `status`, records build-stamped
source revision/dirty state/configuration, backend/adapter/driver/API and requested
versus effective validation. Unknowns are null. The source stamp does not prove
executable byte identity. Legacy v1 bundles predate this environment record.

## Frame provenance

`capabilities.parameterFrameProvenance` advertises frozen `parameters` schema v1
in image sidecars, graph/resource frame metadata and sequence ranges. It records
revision/content identity, non-default typed values and their defaults/sources,
separate requested/active lighting generations, nullable profile id/hash/version
and relevant batch receipts with all four stamps. SameFrame products must agree.

Inputs and profile identities freeze at frame build. Successful matching
submission can add only that frame's `firstConsumed`; later publication, receipt
collection or live queries cannot relabel older artifacts. Unknown/custom lighting
retains null profile fields. Inspect the saved snapshot to explain an old capture.

## Cancellation and ownership

`capture-cancel -JobId` returns `Cancelled | TooLate | Finished | NotFound` apart
from job status/publication. Query and consume the actual terminal receipt.
`TooLate` preserves the eventual outcome. `Cancelled` prevents publication, but
GPU payload/writer retirement can outlive receipt consumption; inspect `status.capture`.

Sequence, startup and interactive owners consume only their own jobs; those ids
are inaccessible through the external pipe. Sequence status exposes
`acceptedCaptureJobs`, `finishedCaptureJobs`, `pendingCaptureJobs`;
`completedCaptures` counts image/resource deliveries, so image + resource + graph
is one job and two deliveries. Range artifacts use a separate writer reservation,
without a SameFrame job. `sequenceArtifactWrites` counts it and
`sequenceTimingWrites` counts the subset with timing. See
[sequence completion/cancellation](sequences.md) and [interactive history](interactive.md)
for their workflow-specific behavior.
