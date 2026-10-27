# Session parameters (protocol v4)

Use `parameters` to discover ready active content and each descriptor's supported
operations. `set` assigns Host or Lab values; `host-view-overrides` sets exact Host
override presence and values. Capabilities are reported by `status`.

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

Integer types require integer JSON numbers; floats must fit the declared range.
A batch has 1-128 distinct ids, each 1-128 UTF-8 bytes. Validation covers the whole
batch and repeats at application. A rejected batch leaves live values unchanged;
post-commit effect failures preserve the committed values and actual receipts.

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

Timeout/polling failure preserves `batchId` for a later query. A lost consuming
response may already have released the slot. `admissionUnknown` means the submit
response was lost; inspect `status.parameterControl.batch` and its requested values
before recovery. Use the [run identity](session-jobs.md#run-checked-requests) for
later commands against the same process.

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
retained; retry supplies consumption without reapplying the operation. Consumption
requires the matching submitted frame. Missing/stale attribution fails; a
contradictory declared effect reports `EffectMismatch`.

A committed different assignment to the same id or a newer owning-domain target
can supersede pending effects; an unrelated revision does not. Post-build requests
cannot relabel built frames. Failed batches close unresolved siblings while
preserving existing evidence. Saved [frame provenance](session-jobs.md#frame-provenance)
ties these effects to captured frames.

## Host display-view overrides

Defaults inherit active content plus copied tooling settings; current values also
include committed Host overrides. With `set`, a first equal assignment preserves
inheritance; an existing override stays present. Precedence is content/tooling,
Host overrides, then frame-local sequence overrides. Lab values apply first and
explicit Host fields win. Other render views retain their content settings.
Exposure/pre-exposure resolve together without editing the authored camera.

An Immediate Lab value masked by Host/tooling or normalized to another built value
reports `Superseded` without `firstConsumed`. Inspect every item: a mixed batch may
commit all values while one masked item prevents success. Discovery and a first
NoChange preserve settling. Receipt `frameSerial` is the render clock;
`frameIndex` is the simulation clock.

Overrides clear on content replacement/restart except the owned Rebuild
continuation below. `set` cannot remove one or pin an inherited-equal value; use
[guarded exact states](#guarded-exact-override-states). Sequence settings overrides are
temporary and do not replace Host values. Reference sampling forces temporal AA
and temporal GTAO inactive. Preview tap, camera input/FOV and other Immediate
fields without frozen projections remain unavailable through `set`.

### Host view control state

Check `status.capabilities.hostViewControlState` before relying on these fields.
Discovery and `status.parameterControl` report `controlState`: the process
`runIdentity` (the same value as `environment.process.runIdentity`), the active
`contentIdentity` and `hostViewRevision`. The revision advances once for each
committed or discarded change to Host view override presence or value. NoChange,
inherited content/tooling/camera changes and queries leave it unchanged.

Each Host view descriptor in discovery adds `hostOverride` (`present`, plus `value`
when present), `resolved` (`value`, `source`, `scope: display-view`) and
`inheritedSource`. `resolved` is the next frame's value before sequence or
reference patches. Sources are `content`, `tooling`, `camera` and `host`; a whole
active tooling block attributes all of its fields to `tooling`.

`status.frame` describes the last planned frame and may lag live control state. It
carries that frame's own `controlState`, a `hostView` array whose `source` can also
be `sequence` or `forced-reference`, the temporal consumer plan, and `outcome`
(`planned`, `submitted` or `aborted`). Capture sidecars record the issued frame's
stamp and `hostView` in their [`control`](session-jobs.md#frame-provenance) record.

### GUI observations

The TAA, Ambient Occlusion and Post Processing Inspectors expose a read-only
**Display-view control** section. It separates inherited values/sources, exact
Host override presence/value, Host-resolved values and the current planned frame
after sequence/reference masks. Equal-value pins still mask inheritance.
The Camera Inspector shows this exposure observation only for the display camera;
other cameras keep their independent authored exposure.

The previous frame carries its own serial, outcome and run/content/revision.
Planned/aborted is not submitted-frame evidence; aborted does not certify that no
GPU work ran. Submitted does not prove GPU completion or pass execution.
Temporal consumer state is separate from an enabled setting.
Pending GUI edits enter a later tick and do not change either frozen observation.
Missing evidence is shown as unavailable. These observations do not enable lazy
parameter tracking or change capture settling.

GUI edits still use the tooling/camera layer. Host override release is not yet a
GUI action; use `host-view-overrides` below when removal is required.

### Guarded exact override states

Check `status.capabilities.hostViewOverrides` first. `host-view-overrides` sets
exact Host view override states for `view.taa.*`, `view.gtao.*` and
`view.exposure.*` (descriptors report `overrideOperationSupported`; Lab and
lighting-profile ids report `false` with `overrideOperationReason`). It takes the
complete `controlState` the request was prepared against and 1-128 distinct typed
entries; unspecified ids stay untouched. The client uses that state's run for
submission and result polling:

```powershell
$state = Join-Path (Get-Location) 'Build/control-state.json'
$pin = Join-Path (Get-Location) 'Build/taa-off.json'
$parameters = powershell -NoProfile -ExecutionPolicy Bypass -File $script parameters -Session check | ConvertFrom-Json
[IO.File]::WriteAllText($state, ($parameters.controlState | ConvertTo-Json -Compress),
    (New-Object Text.UTF8Encoding($false)))
[IO.File]::WriteAllText($pin,
    '[{"id":"view.taa.enabled","type":"bool","present":true,"value":false}]',
    (New-Object Text.UTF8Encoding($false)))
powershell -NoProfile -ExecutionPolicy Bypass -File $script host-view-overrides -Session check `
    -ExpectedStateFile $state -OverridesFile $pin -WaitConsumed
```

A present entry stays pinned even when it equals the inherited value; an entry
`{"id":...,"type":...,"present":false}` removes the override and follows the
inheritance resolved when it applies. `-ExpectedRunIdentity` is optional and must
agree with the state's run when supplied. The operation shares `set`'s external
result slot and waiting options.

| Before → requested | Control state | Effect item |
| --- | --- | --- |
| Already the requested presence and value | Unchanged; the batch is still admitted and retained | `NoChange` |
| Absent → present at the inherited value, or present → absent at an equal inherited value | `hostViewRevision` advances | `NoChange`, no `firstConsumed` |
| Any change of the resolved value | `hostViewRevision` advances | Immediate proof of the new value |

The stamp is checked at admission and at the safe application boundary. Admission
conflicts return `ok: false` and an `errorCode`: `ExpectedContentChanged`,
`ExpectedHostViewRevisionMismatch` or `ExpectedRunChanged`. Content/revision conflicts
include the live `controlState`; run conflicts report the current `runIdentity`.
A conflict after admission ends the existing batch as `Failed`, with
`controlCommit.conflict`. Unsupported ids/types (`HostOverrideUnsupported`), invalid
values (`HostOverrideInvalid`) and `Busy` also reject the whole request.

Results retain requested `overrides` and a `controlCommit` with `expected`,
`before`/`after` stamps, `changed`, `changes[]` and `conflict`. The `after` stamp
exists only after commit. Effect `values` contain the applied targets, including
the inherited value of a removal.

Restore saved presence/value states against the last committed `after` stamp.
An intervening Host edit then conflicts; inherited edits remain in their own
layers. Restoration covers Host intent, while a matching submitted frame verifies
rendered state. For a structural-only change, use the frame's stamp and projection;
its NoChange effect has no `firstConsumed`.

## Content-registered lighting profiles

Discover enum integers through `parameters`. `-1` is a read-only indication of no
registered effective match; setting it rejects the batch. Coastal Atrium registers
`coastal.daylight`; Atmosphere Range registers `atmosphere_range.baseline` and
`atmosphere_range.oblique`.

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

## Protected Lab/profile assignment

`parameters.domainControlStates` returns complete stamps for `lab` and
`lighting-profile`, or null when unavailable. Save the required object as a UTF-8
JSON file and submit typed changes from that same discovery:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File $script assign -Session check `
    -ExpectedStateFile Build/lab-control.json -ChangesFile Build/lab-changes.json -WaitConsumed
```

`assign` accepts one Lab or profile source with the same limits and wait/consume
options as `set`. The stamp binds submission/polling to the run and is checked at
admission and safe commit, even for would-be NoChange. Conflict responses include
`errorCode` and `domainControlState`.

`batch.domainControl` reports expected/before/after stamps, `committed` and conflicts
separately from [effect receipts](#read-effect-receipts). Rediscover after conflicts
or content activation; Rebuild's `after` stamp describes the source commit, not the
target. Host exact states use `host-view-overrides`; legacy `set` remains unguarded.

## Focused verification

Select suites with [RunVerification.ps1](../../../../Tests/Verification/RunVerification.ps1);
`-List` owns the current catalog and supported configurations.

| Suite | Main evidence |
| --- | --- |
| `session-parameters` | GTAO Immediate and texture/Physical Sky Deferred receipts, captured settings/generations and cache-publication paths. |
| `session-host-parameters` | Demo/Lab overrides, mixed-source atomicity, settling, exposure response, result retention and guarded exact states. |
| `protected-assignments` | Lab/profile guards, stale NoChange and shared-domain conflicts; activation and submitted Immediate/Deferred/Rebuild effects. |
| `lighting-profiles` | Profile discovery/transitions, exposure independence and requested/active identities. |
| `lab-sun-parameters` | Authored sun elevation, profile rebasing and NoChange. |
| `session-rebuild-parameters` | Zero/one/64-light fixtures, retained results and built scene/frame provenance. |

These suites run hidden on both RHIs by default. For lighting-only coverage,
invoke `Tests/Verification/TestSessionParameters.ps1 -Scope lighting` directly with
typed `-Rhi` arrays. [TestHostParameters.ps1](../../../../Tests/Verification/TestHostParameters.ps1) accepts `-StateDirectory` for
task-owned verification caches; omit it for isolated state. Scheduled-event suites
are listed in [Sequences](sequences.md#parameter-events).
