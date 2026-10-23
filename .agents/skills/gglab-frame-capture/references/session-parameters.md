# Session parameters (additive protocol v4)

`capabilities.parameterFrameProvenance` advertises frozen frame metadata. Capture
sidecars carry `parameters` schema v1: revision, non-default typed values and their
defaults/sources, separate requested/active lighting generations and nullable
profile identities, plus relevant batch receipts. Build freezes inputs; only a
successful matching submission adds `firstConsumed`. Inspect the artifact instead
of querying current session state to explain an older capture. Every completed
production sequence also requires a `gglab.parameter-frames` v1 range product;
see [sequences](sequences.md) for publication and frame/reference joins.

Use the script and application from the same revision. Check
`status.capabilities.sessionParameterControl` for Host/mixed-source support.
`labParameterControl` retains discovery for the earlier Lab-only extension. The extension adds commands and status fields; existing
capture/result/sequence fields and their v4 meanings are unchanged.

The external session client owns one batch/result slot shared by Host and active Lab sources.
Host `view.taa.*`, `view.gtao.*` and `view.exposure.*` work on Demo and Lab content.
The Lab source supports Immediate parameters with explicit frozen-frame projections
and **one explicitly
bound Deferred lighting parameter per batch**, optionally mixed with Immediate
items. Current bindings are `gtao.ibl.sample_count` and
`atmosphere_range.ibl.sample_count` (uint, 1–4096). They change IBL specular sample
count through the owning Runtime control, preserving generation attribution.
The GTAO fixture covers texture-IBL cache restoration; Atmosphere Range covers
Physical Sky, whose current producer deliberately skips texture caches.

Host `environment.profile` selects content-registered Physical Sky inputs.
Arbitrary authored sun/atmosphere parameter bindings, rebuild transactions and
parameter metadata in saved frame artifacts remain separate work. Bound Deferred
sequence events follow [Sequences](sequences.md#parameter-events).
`set` rejects during an active sequence; a new
sequence rejects while a parameter batch is pending. Do not use external `set`
as a substitute for scheduled events.

## Host display-view overrides

Host ids use snake-case fields. `view.taa.*` exposes the same fields as the
existing sequence Temporal AA overrides. `view.gtao.*` exposes `enabled`, `radius`,
`direction_count`, `step_count`, `temporal_accumulation` and `temporal_max_samples`.
Manual exposure uses `view.exposure.ev100` and `view.exposure.compensation`.
Discover exact ids, ranges and enum integers through `parameters`; enum names
are descriptor labels, while `set` uses the typed integer value.

Defaults inherit the active content plus the frame owner's copied tooling overrides.
Host current values additionally include committed Host overrides. Only explicitly
changed Host fields become overrides. NoChange leaves an inherited field
inherited. Overrides last for the active content identity and clear on content
replacement/restart; this extension has no unset/reset verb. A mixed batch may
set Host and Lab values atomically; one invalid item rejects all sources.
Lab values apply first, then Host display-view overrides take precedence.
Other render views retain their content settings. Exposure and pre-exposure are
resolved together before temporal planning, without editing the authored camera.
Discovery and a first NoChange preserve capture settling. `status.frame.frameSerial`
is the render clock used by receipt stamps; `frameIndex` remains the simulation clock.

Frame precedence is content plus optional tooling settings, Host overrides, then
legacy sequence overrides. Reference sampling continues to force temporal AA and
temporal GTAO inactive. Legacy sequence values use the shared Runtime typed settings operations,
are temporary, and do not replace session Host values. External `set` remains
blocked throughout a sequence. Immediate events use the same Session transaction owner;
see [Sequences](sequences.md#immediate-parameter-events).

Immediate NoChange is checked against frozen frame inputs before becoming
terminal. A changed or unchanged stored Lab value masked by Host/tooling, or
normalized to another value, reports `Superseded` with no `firstConsumed`.
Inspect every item: a mixed batch can commit all source values while its masked
Lab item prevents `batch.succeeded`. This does not roll back committed values.

Check each descriptor's `setSupported`. GTAO projects its eleven GTAO settings
and camera near/far from built view inputs; Atmosphere Range projects aerial
perspective and TAA enabled. Preview tap, camera input/FOV and Immediate fields
without a source projection are unavailable through session `set`. The existing
Lab interactive controls retain their owning-domain behavior. Deferred lighting
bindings retain their separate generation/publication evidence.

## Content-registered lighting profiles

Discover `environment.profile` and its enum integers through `parameters`.
The current enum matches Runtime's requested **effective** inputs; `-1` means
no registered effective match and is read-only. Setting it rejects the batch.
Registration rejects duplicate IDs/hashes and effective-equivalent aliases, so
current selection is unambiguous. Source/type binding distinguishes this Host
enum from a Lab-owned parameter with the same name when no Host profile exists.
Only ready active content registers profiles; content replacement clears the
registrations and requested/active identities. Coastal Atrium registers
`coastal.daylight`; Atmosphere Range registers `atmosphere_range.baseline` and
`atmosphere_range.oblique` as transition fixtures, not production Golden Hour values.

`parameters.lightingProfiles` lists IDs, SHA-256 input hashes, `hashVersion: 2`
and nullable `recommendedEV100`. Hash v2 encodes explicit little-endian float32
fields, canonicalizes direction through the producer's rotation/extraction path,
and normalizes signed zero; other authored fields remain raw. Profile ID and
exposure advice are excluded. This supersedes the uncommitted v1 contract, which
distinguished raw direction magnitudes even when the producer discarded them.
The first contract supports Physical Sky with IBL intensity 1 and rotation 0.
Profile application changes the designated sun and atmosphere, not manual exposure.
Recommended exposure is advisory only. A profile requires an existing designated sun.
Strict numerical validation belongs to WorldSun, Atmosphere and Camera; profiles
compose it with producer support and the positive-illuminance profile policy.
Registrations, prepared inputs, hashes and enum labels are cached for the content
identity. Invalid registrations omit only this control: discovery's
`lightingProfileError` and `status.parameterControl.lightingProfileError` explain
the configuration error, while unrelated Host controls remain available.

The same Session batch accepts one profile item, optionally with Host/Lab Immediate
items; two Deferred lighting items reject atomically. Re-selecting current inputs
does not rebake or rewrite equivalent authoring representations. An already active
target confirms NoChange; a pending target still awaits publication and consumption.
Quaternion sign, direction magnitude and normalized chromaticity are not compared
as arbitrary representation bytes when deciding effective equality. Profile
application uses an explicit Runtime World Lighting control at the safe boundary,
separate from frame/pipeline access. Existing authored edits settle **before** the
batch's attribution baseline; a changed profile must then prove its own actual
request. No generations are predicted and no redundant rebake is forced. Normal
frame building revalidates/deduplicates.

Both discovery and `status.parameterControl.lighting` expose separate `requested`
and `active` records (`profileId`, `profileHash`, actual `generation`), or null when
unknown/custom. The old active identity remains attached to its published generation
while a newer request is pending; the new profile hash never relabels an old generation.
Identity comes from immutable authored-input snapshots retained by the actual
Runtime request/publication, not the current mutable World. Effective equality
alone does not prove a profile's authored hash: an equivalent selection can be
NoChange while its profile identity stays unknown, or retains the older request's
proven identity. The persisted v2 hash still distinguishes other raw authored
fields (such as chromaticity before normalization); it is not the Runtime
effective-source identity.
These records describe the published environment-lighting product, not a claim that
every direct-light input is delayed until that publication. They are live control
snapshots; use the saved frame's `parameters` for capture/sequence/export provenance
as described in [sequences](sequences.md).

Atmosphere Range also exposes the Lab-owned Deferred float
`atmosphere_range.sun.elevation_degrees` (-90 to 90). It preserves the current
azimuth and other lighting inputs; a mixed batch still permits only one Deferred
lighting item. `RunVerification.ps1 -Suite lab-sun-parameters` exercises this binding,
including profile rebasing, repeated NoChange and its frame/receipt evidence.

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
still be Busy or require a ready Lab. No ready active content returns `NoParameterSource`.

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
before the actual Ready frame. The external client retains one AppRuntime session-owned
accepted batch/result slot; terminal results retain it until explicit consumption. Rejection allocates
no batch id. IDs are process-local and **separate from capture `jobId`** and
transport correlation `id`.

Internally, Session separates one queued source reservation from a bounded set of
128 retained transaction records, including terminal results. The same internal
owner may admit another batch after application while older effects await domain
publication or successful submission. Owner identity plus batch ID controls query,
collection and cancellation; external commands cannot claim sequence records.
Each batch retains its own revision, values, effect evidence and four stamps.
A later unrelated revision does not supersede an older pending item. A committed
different assignment to that item's ID, or a newer owning-domain target before
build, supersedes it without inventing consumption. Owner termination closes every
outstanding item and releases only the queued reservation. This storage capability
does not itself grant readiness exceptions. Deferred sequence events use the
owner- and generation-bound policy in [Sequences](sequences.md#parameter-events).

At the Ready boundary, the Session prepares the complete current batch before
committing any of its live values: source/type bindings, Host candidates and
overrides, the Lab's queued reservation/candidate, World Lighting target and
input plan, revision and receipt storage. Preparation does not invoke Lab
handlers, consume the Lab command or start the batch's lighting request. A
preparation rejection leaves those live values and revisions unchanged, including
manual exposure when a designated sun disappeared after admission. Settling an
already-authored external World edit happens before this batch's baseline and
is not a request created by the rejected batch.

Commit installs all source values without intervening content callbacks or
fallible validation. Lab handlers execute afterward, so they cannot observe a
half-committed mixed batch. Explicit Host display-view values still take
precedence over Lab view changes; an inherited NoChange remains inherited unless
the Lab effect actually changes that field. Post-commit effect failure,
supersession and GPU publication/submission failure remain receipt outcomes, not
a promise of GPU rollback. Host view, Lab and lighting-profile source adapters
own their candidate changes and evidence translation; Session alone owns the
batch receipts/result. World authoring control is supplied through Runtime
composition access, not ordinary pipeline RenderServices. Busy admission releases
only reservations acquired by that attempt, preserving any pre-existing source
reservation even if its numeric ID coincides.

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
even if application fails or content is replaced. Results remain queryable
through the session owner after replacement and retain their original content id.
Process termination ends retention. LabRuntime owns its safe queued mutation,
without holding a second result or receipt cache.

`batch` includes source (`host`, `lab` or `mixed`)/content, state (`Queued`, `Applying`, `Finished`,
`Failed`), revision (null until application), fixed supported impact `Immediate`,
owned `values`, `items`, failure and terminal/success indicators. The impact
means no Lab rebuild; it is independent of the per-item effect class. Each effect item
reports its declared class, actual revision/domain/generation identity, state,
requested/applied/published/firstConsumed stamps and failure. Immediate
`published == applied`; only the exact successfully submitted frame state can
supply `firstConsumed`. `NoChange` records requested/applied confirmation but no
publication/consumption and introduces no revision or new bake request.
Superseded, failed and unreached effects are terminal without successful
consumption. Capability is declared independently from operation evidence. After
all sources commit, the explicit Deferred handler returns this synchronous
operation's Runtime outcome and target identity. A new request must cover the
apply boundary; the owning operation may issue more than one request. An unchanged
operation binds the existing requested target without another bake. Unsupported,
missing or stale attribution fails; it is never inferred from an earlier cached
request, the descriptor or the `ibl` readiness gate. A mixed batch retains per-item
classes and stamps.

The Runtime frame builder freezes the published lighting generation in its
`RenderScene`. That immutable scene state, followed by successful submission,
establishes Deferred consumption. Publication observed in an aborted build stays
recorded, while retry alone can supply firstConsumed. New requests/values can
supersede pending states; a post-build request cannot relabel an already built
frame. A terminal failed batch closes unresolved siblings without erasing their
existing evidence. Accepted application/effect failure does not roll back live
values or pretend already-started GPU work was cancelled.

Deferred NoChange requires unchanged effective intent, a fresh operation result,
and an already active matching Runtime target. It confirms without a new request
or publication/firstConsumed stamp. Equal stored values whose target is still
pending bind that target and await actual publication and submitted consumption.
A Deferred operation runs even for equal stored values; an unrelated Immediate
batch never invokes the Deferred binding. The fixtures change global bake quality
only while entered and restore the owned
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

`Tests/Verification/RunVerification.ps1 -Suite lighting-profiles` checks Coastal
Atrium registration and Atmosphere Range baseline/oblique/baseline transitions on
both RHIs, atomic rejection, mixed profile/Lab Immediate/Host NoChange commits,
exposure independence, actual generation receipts, captured generations and
uncached Physical Sky publication logs. Headless
AppRuntime coverage additionally checks prepare-time loss of a Lab reservation
or designated sun, mutation-free rejection, complete commit visibility to Lab
handlers, Host precedence after Lab effects, source/type collisions, registration
errors/cache lifetime, effective equivalence, unresolved authored edits,
requested/active separation, supersession, publication on abort/retry, shutdown
and running content replacement. The synthetic headless
producers exercise real scheduler/fence/receipt ownership, not native shader products.


`Tests/Verification/RunVerification.ps1 -Suite session-host-parameters` checks
Host discovery, atomic mixed-source rejection, submitted receipts, NoChange,
result retention, captured settings, exposure image response, exact settling origin
across discovery/first NoChange, and sequence exclusion on Coastal Atrium Demo and
GTAO Lab, using both RHIs by default. The suite primes clean ready history through
an after-ready capture, then compares each content's authored exposure with +2 EV.
Direct invocation accepts `-StateDirectory` to reuse task-owned verification caches;
omit it for a new isolated state directory. Captures and receipts stay run-local.


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
not arbitrary sun inputs, scheduled-event, cross-backend identity, performance
or real driver-fault proof.
