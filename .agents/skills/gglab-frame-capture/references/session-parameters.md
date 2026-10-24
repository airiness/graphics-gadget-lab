# Session parameters (protocol v4)

Use `parameters` to discover ready active content, then submit typed changes with
`set`. Check `status.capabilities.sessionParameterControl` and each descriptor's
`setSupported`; discovery is not a promise that every authored field is editable.

## Discover and set

With an existing GTAO session named `check`:

```powershell
$script = 'Scripts/Verification/GGLabSession.ps1'
powershell -NoProfile -ExecutionPolicy Bypass -File $script parameters -Session check
# A UTF-8 file avoids shell quoting; retain the outer array for one item.
$changesFile = Join-Path (Get-Location) 'Build/radius-changes.json'
[IO.File]::WriteAllText($changesFile,
    '[{"id":"gtao.radius","type":"float","value":2.0}]',
    (New-Object Text.UTF8Encoding($false)))
powershell -NoProfile -ExecutionPolicy Bypass -File $script set -Session check `
    -ChangesFile $changesFile -WaitConsumed
```

Discovery returns `contentId`, `revision` and descriptors with source, type,
impact/effect class, edit policy, current/default values, ranges, enum integers
and `setSupported`. No ready active content returns `NoParameterSource`.

| Control | Supported scope |
| --- | --- |
| Host `view.taa.*`, `view.gtao.*` | Display-view overrides on Demo and Lab content. |
| Host `view.exposure.ev100`, `view.exposure.compensation` | Manual exposure, independent of lighting profiles. |
| Host `environment.profile` | Content-registered Physical Sky profiles. |
| Lab Immediate fields | Fields with explicit frozen-frame projections; inspect `setSupported`. |
| `gtao.ibl.sample_count`, `atmosphere_range.ibl.sample_count` | Deferred uint, 1-4096; texture IBL and Physical Sky respectively. |
| `atmosphere_range.sun.elevation_degrees` | Deferred float, -90 to 90; preserves azimuth and other lighting inputs. |
| Lab Rebuild fields | Supported `RebuildScene`, `RecreatePipeline` or `RestartSession` descriptors. |

Each change has exactly `id`, `type`, `value`; types match the descriptor:

| Type | JSON value |
| --- | --- |
| `bool` | Boolean, without numeric/string coercion. |
| `int`, `enum` | Signed 32-bit integer; enums use declared integers, not labels. |
| `uint` | Unsigned 32-bit integer. |
| `float` | Finite number representable as float32. |
| `vector3`, `color` | Three finite float components or four in RGBA order respectively. |

Integer types reject fractional/exponent JSON numbers even when mathematically
integral. Floats are not range-clamped. A batch has 1-128 distinct ids, each
1-128 UTF-8 bytes. Unknown ids, invalid values/types/ranges and unsupported effects
reject the whole batch without changing live values or revision. Complete-batch
preparation revalidates at application; post-commit effect failures are receipt
outcomes and do not roll back committed values or already-started GPU work.

A batch accepts at most one bound Deferred lighting item, optionally with
Immediate items. Rebuild may accompany supported Immediate values, but Rebuild
with Deferred lighting rejects atomically. `set` rejects during an active
sequence; a sequence rejects pending external parameter work, and event sequences
also reject retained terminal results. Consume those first and use
[scheduled events](sequences.md#parameter-events) for in-sequence changes.
Startup `-LabParameters` uses a separate Debug/Develop-only string parser.

## Wait, query and consume

Admission immediately returns `batchId` and a `Queued` batch; rejection allocates
no batch id. The external client has one retained batch/result slot shared by
Host and Lab sources. Session owns receipts; terminal results hold the slot until
consumed. `batchId`, capture `jobId` and transport correlation `id` are separate
process-local namespaces. External commands cannot claim sequence-owned batches.

The script waits for application by default. `-NoWait` returns admission only;
`-WaitConsumed` waits for every effect to become terminal, including failure or
supersession, and cannot be combined with `-NoWait`. Waiting consumes terminal
results unless `-KeepResult`; applied nonterminal results require later query
and consumption. Require `batch.terminal` and `batch.succeeded: true` for successful
effects; `ok` and exit 0 describe only the control operation.

```powershell
$queued = powershell -NoProfile -ExecutionPolicy Bypass -File $script set `
    -Session check -ChangesFile $changesFile -NoWait | ConvertFrom-Json
if (-not $queued.ok) { throw ($queued | ConvertTo-Json -Depth 10) }
powershell -NoProfile -ExecutionPolicy Bypass -File $script parameter-result `
    -Session check -BatchId $queued.batchId
powershell -NoProfile -ExecutionPolicy Bypass -File $script parameter-result `
    -Session check -BatchId $queued.batchId -Consume
```

Pending consumption never releases the slot. Unknown/consumed/other-owner ids
return `status: not-found`. Results retain admitted values and original content
identity through failure or content replacement; process termination ends retention.
`status.parameterControl` exposes availability, revision and the same retained batch.

Timeout/polling failure preserves `batchId`; query it without resubmitting. A lost
consuming response may already have released the slot. Lost admission reports
`admissionUnknown`: inspect `status.parameterControl.batch` and its values before
recovery. It is not an idempotency key; do not blindly resubmit.

## Read effect receipts

Inspect each item's class, actual revision/domain/generation identity, failure,
and `requested`, `applied`, `published`, `firstConsumed` stamps. Batch `impact`
reports actual changed Lab impact; `Immediate` impact means no Lab rebuild,
independently of an item's effect class.

| Effect | Publication and consumption evidence |
| --- | --- |
| Immediate | `published == applied`; matching frozen inputs in a successfully submitted frame establish `firstConsumed`. |
| Deferred lighting | Actual Runtime request/publication generation, then a matching frozen scene in a successfully submitted frame. |
| Rebuild | `lab-session` operation generation, exact candidate activation, then its successfully submitted target frame. |
| NoChange | Requested/applied confirmation without a new revision, publication or consumption stamp. |

Equal stored Deferred values are NoChange only when effective intent is unchanged
and the matching Runtime target is already active. A still-pending target must
await publication and consumption. Publication observed on an aborted frame is
retained; retry supplies consumption without reapplying the operation. Never infer
consumption from readiness, application or a predicted serial. Missing/stale
attribution fails; a contradictory declared effect reports `EffectMismatch`.

A committed different assignment to the same id or a newer owning-domain target
can supersede pending effects; an unrelated revision does not. Post-build requests
cannot relabel built frames. Failed batches close unresolved siblings while
preserving existing evidence. Use saved [frame provenance](session-jobs.md#frame-provenance)
to explain artifacts, rather than the session's current mutable state.

## Host display-view overrides

Defaults inherit active content plus copied tooling settings; current values also
include committed Host overrides. Only changed Host fields become overrides;
NoChange preserves inheritance. Precedence is content/tooling, Host overrides,
then legacy sequence overrides. Lab values apply first and explicit Host fields
win. Other render views retain their content settings. Exposure/pre-exposure
resolve together without editing the authored camera.

An Immediate Lab value masked by Host/tooling or normalized to another built value
reports `Superseded` without `firstConsumed`. Inspect every item: a mixed batch may
commit all values while one masked item prevents success. Discovery and a first
NoChange preserve settling. Receipt `frameSerial` is the render clock;
`frameIndex` is the simulation clock.

Overrides clear on content replacement/restart except the owned Rebuild
continuation below; there is no unset/reset verb. Legacy sequence overrides are
temporary and do not replace Host values. Reference sampling forces temporal AA
and temporal GTAO inactive. Preview tap, camera input/FOV and other Immediate
fields without frozen projections remain unavailable through `set`.

## Content-registered lighting profiles

Discover enum integers through `parameters`. `-1` is a read-only indication of no
registered effective match; setting it rejects the batch. Coastal Atrium registers
`coastal.daylight`; Atmosphere Range registers `atmosphere_range.baseline` and
`atmosphere_range.oblique` transition fixtures. Golden Hour values belong to GI P5.

Profiles require a designated sun and support Physical Sky with IBL intensity 1
and rotation 0. They change sun/atmosphere, not manual exposure;
`recommendedEV100` is advisory. Invalid registrations omit this control and expose
`lightingProfileError` in discovery/status without disabling other Host controls.
Registrations clear with content replacement.

`parameters.lightingProfiles` supplies ids, SHA-256 hashes and `hashVersion: 2`.
Discovery and `status.parameterControl.lighting` keep separate `requested` and
`active` profile id/hash/generation records, null when unknown/custom. A pending
request never relabels the old active generation. Effective equality may confirm
NoChange without proving a profile hash; the authored hash is distinct from
Runtime's effective-source identity. These identities describe environment-lighting
publication, not delayed publication of every direct-light input.

## Rebuild transactions outside sequences

Multiple changed Rebuild items create one operation at their maximum changed
impact. Unchanged items start no operation. Admission closes during candidate
preparation/warmup; requested Lab values and the old active Lab remain distinct.
Host overrides survive the owned continuation. Lab Immediate companions await
target activation's frame proof; Host Immediate items can be consumed earlier.
Use `-WaitConsumed` to include activation/consumption in the result.

Factory/preparation failure preserves the old active Lab without rolling back
committed Host values. Aborted target frames retain publication and retry without
another operation. Unrelated content switches supersede the wait; shutdown closes
unresolved effects. Sequence events reject Rebuild.

## Focused verification

Select suites with [RunVerification.ps1](../../../../Tests/Verification/RunVerification.ps1);
`-List` owns the current catalog and supported configurations.

| Suite | Main evidence |
| --- | --- |
| `session-parameters` | GTAO Immediate and texture/Physical Sky Deferred receipts, captured settings/generations and cache-publication paths. |
| `session-host-parameters` | Demo/Lab overrides, mixed-source atomicity, settling, exposure response and result retention. |
| `lighting-profiles` | Profile discovery/transitions, exposure independence and requested/active identities. |
| `lab-sun-parameters` | Authored sun elevation, profile rebasing and NoChange. |
| `session-rebuild-parameters` | Zero/one/64-light fixtures, retained results and built scene/frame provenance. |

These suites run hidden on both RHIs by default. For lighting-only coverage,
invoke `Tests/Verification/TestSessionParameters.ps1 -Scope lighting` directly with
typed `-Rhi` arrays. [TestHostParameters.ps1](../../../../Tests/Verification/TestHostParameters.ps1) accepts `-StateDirectory` for
task-owned verification caches; omit it for isolated state. Scheduled-event suites
are listed in [Sequences](sequences.md#parameter-events). Headless fault tests
exercise ownership/failure paths; native receipts and artifacts establish GPU effects.
