# Session capture jobs and artifacts (protocol v4)

Requests/responses carry an unsigned transport correlation `id`; responses carry
`ok`. Parameter control uses separate
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
identity or receipt. Retry Busy requests when capacity becomes available; revise
rejected plans before resubmitting. Accepted jobs wait for payload/writer capacity.

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

Client timeout/polling errors preserve accepted `jobId` for query or cancellation.
Admission transport timeout is at least 30 seconds or the larger `-TimeoutSeconds`;
polling uses the remaining wait budget. A lost admission response leaves admission
uncertain; a lost consuming response may have released the receipt. Recover
confirmed handles against the same run. Polling counts as activity; stored receipts
do not.
Termination drains/cancels work and ends retention, leaving artifacts on disk.
Transport failures still print one JSON error result and exit 1.

### Run-checked requests

A session name and its numeric handles can be reused after a process restart.
`start.runIdentity` and `status.environment.process.runIdentity` identify the run.

Before returning `ownership: created`, `start` retains the launched process handle
and confirms that a successful status response names the same session and PID,
contains a valid run identity, and still belongs to a live created process.
Readiness alone does not establish that ownership.

If startup status cannot confirm it, the client returns
`errorCode: StartIdentityUnconfirmed`, `runIdentity: null` and no creation ownership,
without writing a new `session.json` record. It attempts to terminate only the
created process through its retained handle, never by sending `stop` to the named
endpoint. A non-null `cleanupError` means that cleanup failed; inspect the returned
PID and output log before explicit recovery. Do not adopt a replacement run from
a later status query as a process this workflow created.

| Request | Run binding |
| --- | --- |
| Commands listed in `status.capabilities.runIdentityChecks.commands` | Pass `-ExpectedRunIdentity` from the intended run. |
| [`host-view-overrides`](session-parameters.md#guarded-exact-override-states) | The required expected state supplies the run for submission and polling. |
| `capture`, `graph` or `export` with a frame predicate | `requiredFrameState.runIdentity` supplies the run for submission and polling. |

An explicit `-ExpectedRunIdentity` must agree with a state or predicate's run.
`ExpectedRunChanged` rejects before handle access and reports the current run;
stop recovery against that endpoint. `RunIdentityUnavailable` means the session
cannot provide the required run check. Later manual queries and cancellation use
the originally recorded run identity.

## Interpret results and artifacts

Require `status: completed` and `publication: Published`, then validate the final
`manifest` and required files. `ok` reports the control response, including queries
of failed or cancelled jobs.

Terminal receipts retain `jobId`, status/publication, `bundleId`, `manifest` and
ordered `products`. Each product has `index`, `kind`, status/publication, `files`,
`failure`, `failureCode` and nullable frame `metadata`. Select files by product
kind/index. Top-level frame/watch/failure fields summarize the products. Top-level
`failureCode` names a job failure such as `RequiredFrameStateMismatch`;
`products[].failureCode` is a resource-observation code. Artifact
`metadata.requestId` and session `jobId` are separate identities.

```powershell
Import-Module ./Scripts/Verification/VerificationSupport.psm1
$png = Get-VerificationCaptureFile $result '.png' 0
python Scripts/Verification/ValidateCaptureBundles.py $result.manifest
```

| Artifact | Current writer / compatibility |
| --- | --- |
| `gglab.capture-bundle` | v3; readers retain v1/v2. |
| Image JSON sidecar | v1, with nested `parameters` v1 and `control` v1. |
| `gglab.render-graph` | v7 adds typed AS resources; readers retain v1-v6. Parameter provenance is optional in v7 and required in v6. |
| `gglab.resource-export` | v3; readers retain v1/v2 without parameter provenance. |
| `gglab.parameter-frames`, `gglab.gpu-timing` | v1 sequence-range products; see [Sequences](sequences.md#published-range-artifacts). |

The manifest's `environment`, also exposed by `status`, records build-stamped
source revision/dirty state/configuration, backend/adapter/driver/API and requested
versus effective validation. Unknowns are null; older bundles can omit this record.
Record an executable hash separately when byte identity matters.

## Frame provenance

`capabilities.parameterFrameProvenance` advertises frozen `parameters` schema v1
in image sidecars, graph/resource frame metadata and sequence ranges. It records
revision/content identity, non-default typed values and their defaults/sources,
separate requested/active lighting generations, nullable profile id/hash/version
and relevant batch receipts with all four stamps. SameFrame products must agree.

Inputs and profile identities freeze at frame build. Successful matching submission
can add that frame's `firstConsumed`. Unknown/custom lighting retains null profile
fields. Use the saved snapshot when interpreting a capture.

Image sidecars and graph/resource frame metadata also carry `control` v1: the
issued frame's Host view control stamp, its final `hostView` values with resolver
sources and override presence, and the admitted plan's `requiredFrameState` or null.
The manifest and SameFrame products carry matching records. An absent `control`
record means this evidence is unavailable.

### Required frame state

Check `status.capabilities.requiredFrameState`. `capture`, `graph` and `export` accept
`requiredFrameState` (`-RequiredFrameState`/`-RequiredFrameStateFile`):

```json
{
  "runIdentity": "0123456789abcdef0123456789abcdef",
  "contentIdentity": 481,
  "minHostViewRevision": 13,
  "values": [{ "id": "view.taa.enabled", "type": "bool", "value": false, "source": "host" }],
  "temporalConsumers": [{ "consumer": "temporal-aa", "status": "disabled", "disableReason": "not-requested" }],
  "referenceSampling": false
}
```

`values` holds 1-128 Host view entries, compared exactly with the frame's final
`hostView` after sequence and reference patches; an optional `source` compares the
resolver layer. Consumer, status and reason names are those of `status.frame`; omit
`disableReason` to accept any. Build the predicate from one consistent observation
or a confirmed `controlCommit.after`. The admitted predicate remains fixed through
waiting, retries and publication, independently of parameter-result retention.

| Check | Outcome |
| --- | --- |
| Invalid values, duplicate entries or contradictory requirements | Reject before allocating a job with `RequiredFrameStateInvalid`. |
| Content activation differs or minimum revision exceeds the committed revision | Reject with `RequiredFrameStateUnavailable`. A run check can reject earlier with `ExpectedRunChanged`. |
| Due frame is below the minimum Host revision | Keep waiting. |
| Due frame contradicts the run, content, values, sources or temporal state | Fail before issue with `failureCode: RequiredFrameStateMismatch` and details in `failure`. |
| Due frame matches | Issue; a retry checks the same predicate on its new frame. |

Predicate captures accept `frameSelectionTimeoutMs` (`-FrameSelectionTimeoutMs`),
an integer from 1 to 300000, default 300000. One absolute admission deadline covers
readiness, settling, capacity waits and retries, including while minimized.
`FrameSelectionExpired` releases active-job capacity and retains the failure until
consumed. Successful matching submission ends the deadline; GPU/readback and
publication then follow their existing lifetimes.

The published `control` record pairs the predicate with the issued observation;
`ValidateCaptureBundles.py` checks their agreement offline.

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
