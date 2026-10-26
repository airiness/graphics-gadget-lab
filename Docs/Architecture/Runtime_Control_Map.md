# Runtime Control and Frame Evidence
Atlas ID: atlas.application.runtime-control

- Question: who writes Host view overrides, how is the display view resolved, and
  which existing evidence ties a captured frame to that state?
- VerifiedAgainst: `cdcb3caddffb1fb85f2c227ed262887f3b0ec654`
- Refresh triggers: Host parameter bindings or writers, tooling snapshot inputs,
  content continuation, display-view precedence, capture issue order, provenance
  or owning regression targets change.

Frame stages after issue are in the [frame execution map](Frame_Execution_Map.md).

## Owners and entry points

| Owner | Interface and implementation | Responsibility |
| --- | --- | --- |
| `WinAppHost` | [ApplicationControlParameters.cpp](../../Sources/WinAppHost/Application/Control/ApplicationControlParameters.cpp), [DevToolsRuntime.cpp](../../Sources/WinAppHost/DevTools/DevToolsRuntime.cpp) | Wire adaptation and whole-block GUI tooling overrides. |
| `GGLabAppRuntime` | [GGLabAppRuntime.h](../../Sources/GGLabAppRuntime/GGLabAppRuntime.h), [SessionParameterControl.h](../../Sources/GGLabAppRuntime/Parameters/SessionParameterControl.h), [source adapters](../../Sources/GGLabAppRuntime/Parameters/ParameterSourceAdapters.h) | Session admission, the process run identity, source binding, the private Host override set and its revision, transactions and effect receipts. |
| `GGLabAppRuntime` | [TickFrame](../../Sources/GGLabAppRuntime/GGLabAppRuntimeFrame.cpp), [CapturePlan.h](../../Sources/GGLabAppRuntime/Capture/CapturePlan.h), [FrameCaptureCoordinator.h](../../Sources/GGLabAppRuntime/Capture/FrameCaptureCoordinator.h) | Display-view planning, sequence masking, capture issue and job/publication coordination. |
| `GGLabRuntime` | Public [ViewRenderSettings.h](../../Sources/GGLabRuntime/Public/GGLabRuntime/Graphics/ViewRenderSettings.h), [ViewRenderSettingsPatch.h](../../Sources/GGLabRuntime/Public/GGLabRuntime/Graphics/ViewRenderSettingsPatch.h), [TemporalFramePlan.h](../../Sources/GGLabRuntime/Public/GGLabRuntime/Graphics/Pipeline/TemporalFramePlan.h) | Typed normalization, exposure derivation and temporal consumer/service resolution, independent of parameter strings or session handles. |

These owners are declared in the [AppRuntime](../../Projects/GGLabAppRuntime/GGLabAppRuntime.vcxproj),
[WinAppHost](../../Projects/WinAppHost/WinAppHost.vcxproj) and
[Runtime](../../Projects/GGLabRuntime/GGLabRuntime.vcxproj) projects.

## Host override writers

`SessionParameterControl` owns the Host override set through its private
`ParameterSourceAdapters::m_View`. All writes use this path:

| Entry | Path to the Host override set |
| --- | --- |
| External `set` | WinAppHost converts descriptor-checked values to Assign entries and calls `GGLabAppRuntime::RequestParameters`. |
| External `host-view-overrides` | WinAppHost converts exact states to guarded Pin/removal entries and calls the same entry. AppRuntime checks the run; Session checks content identity and `hostViewRevision`. |
| Scheduled parameter event | Whole-plan validation normalizes event values. `TickFrame` builds typed Assign entries through the read-only `MakeAssignmentRequest` and submits them to `SessionParameterControl::RequestParameters` with the sequence owner ID. |
| Safe application | `OnFrameReady` calls `ApplyParameterBatch`; the Host prepared source stages a copy, `Commit` replaces `m_Overrides`, and `FinalizeEffects` removes newly introduced equal-value overrides. An exact-state batch first rechecks its stamp, then `PrepareOverrides` sets each requested presence, including pins equal to the inherited value and removals, without that equality removal. |
| Content replacement or release | `SynchronizeContent` calls `ParameterSourceAdapters::Bind`; without continuation it calls `HostViewParameterSource::Bind`, which clears `m_Overrides`. |
| Shutdown | `SessionParameterControl::Finish` ends batches and binds null sources, clearing the same set. |

The storage writes are in
[HostViewParameterSource.cpp](../../Sources/GGLabAppRuntime/Parameters/HostViewParameterSource.cpp);
the safe boundary and binding policy are in
[SessionParameterControl.cpp](../../Sources/GGLabAppRuntime/Parameters/SessionParameterControl.cpp).
`Refresh` rebuilds the discovery projection without changing override presence.

[ParameterRequest.h](../../Sources/GGLabAppRuntime/Parameters/ParameterRequest.h)
defines Assign, Pin and RemoveOverride intent. External requests share AppRuntime
sequence exclusion and one retained result slot. Session's `AdmitParameterRequest`
validates the request, reserves sources and stores an owned const copy in the batch.
Safe application reads that request; `values` and `m_HostViewOperation` are result
projections.

`SessionParameterControl` advances `hostViewRevision` at the same sites: once after
a committed batch whose finalized override set differs, and once when content
binding or shutdown clears a non-empty set. Identical override states, inherited
value changes and queries leave it unchanged.

`ParameterBatchStatus::m_HostViewOperation` records the control commit separately
from effect receipts. `EndForContentChange` rejects an unapplied operation prepared
against a replaced activation. Command usage and result fields are in
[session parameters](../../.agents/skills/gglab-frame-capture/references/session-parameters.md#guarded-exact-override-states).

Other layers have separate storage:

- GUI panels edit `DevToolsRuntime::m_ViewRenderSettingsOverrides`; its
  `CaptureFrameSettings` copies active whole blocks into an AppRuntime snapshot.
- Sequence TAA/GTAO overrides patch a frame-local settings value in `TickFrame`.
  Scheduled `view.*` parameter events use Session control.

`environment.profile` is a separate, Deferred Host source. Lab authoring writes can
change inherited values but never write Host view overrides.

## Protected Lab/profile assignments

WinAppHost translates `assign` to single-domain Assign requests carrying
`ParameterRequest::m_ExpectedDomain`. AppRuntime checks the run;
`SessionParameterControl` validates the stamp at admission and safe commit.

The stamp combines Session binding and Lab owner/value revisions with
[World authoring](../../Sources/GGLabRuntime/Public/GGLabRuntime/Core/World.h) and
[environment control](../../Sources/GGLabRuntime/Private/Graphics/EnvironmentLightingSystem.cpp)
revisions. World sun/atmosphere writers call `MarkLightingAuthoringChanged`;
environment settings and lease changes track intent rather than bake publication.

`ParameterBatchStatus::m_DomainOperation` separates control commit/conflicts from
effect receipts. Rebuild uses its [owned continuation](#content-activation-and-continuation);
target edits require a fresh stamp. Discovery, conflicts and waiting are described
in [session parameters](../../.agents/skills/gglab-frame-capture/references/session-parameters.md#protected-labprofile-assignment).

## Display-view resolution

[ResolveApplicationViewSettings](../../Sources/GGLabAppRuntime/ApplicationToolingIntegration.cpp)
calls `ApplicationToolingFrameSettings::ResolveProfile`, then Runtime's
[ResolveViewRenderSettings](../../Sources/GGLabRuntime/Private/Graphics/ViewRenderSettings.cpp).
For one effective display view, the inherited settings are a pure function of:

1. The active content's authored `ViewRenderProfile`.
2. The selected display camera's `GetManualEV100()` and
   `GetExposureCompensationEV()`.
3. The per-tick tooling optionals `m_TemporalAA`, `m_GTAO`, `m_Bloom` and
   `m_ScenePreExposure`, including their presence and values.

The display-view ID and selected camera identify which inputs apply.
`ApplicationViewSettingsResolution::m_ToolingBlocks` records which whole tooling
blocks replaced authored settings. `m_ScenePreExposure` changes the derived exposure
storage scale; manual exposure comes from the display camera.

`GGLabAppRuntime::Tick` captures tooling once before `TickFrame`. The resolver
lambda composed in [InitializeServices](../../Sources/GGLabAppRuntime/GGLabAppRuntimeServices.cpp)
and frame planning both use that `m_ToolingFrameSettings`, including after Lab
parameter effects; GUI changes made while drawing reach a later tick's snapshot.

```text
content update and safe parameter application
  -> capture reference-view restoration / sequence camera pose
  -> select effective display view
  -> authored profile + tooling blocks + display-camera exposure
  -> Host view overrides
  -> reference sampling masks OR sequence settings patches
  -> temporal consumer plan and transaction
  -> BuildCaptureFrameState -> FrameCaptureCoordinator::BeginFrame (issue)
  -> BuildFrame -> graph build/record -> EndFrame -> submitted feedback
```

`BuildFrame` copies `m_DisplayViewSettings` for the selected main/debug view in
[RenderFrameBuilder.cpp](../../Sources/GGLabRuntime/Private/Graphics/RenderFrameBuilder.cpp).
Other rendered views resolve separately and do not receive Host overrides.

### Host view bindings and the layers that patch them

[HostViewParameters.cpp](../../Sources/GGLabAppRuntime/Parameters/HostViewParameters.cpp)
declares the Immediate Host view bindings, types, ranges and enum members.
`DescribeHostViewProjection` attributes each binding to the last layer that wrote
it: inherited `content`, `tooling` or `camera`, then `host`, then `sequence` or
`forced-reference`. Attribution follows writes even when values are equal.
Reference sampling and sequence settings patches are mutually exclusive.

| Host bindings | Inherited layer | Sequence settings patch | Reference mask |
| --- | --- | --- | --- |
| `view.taa.enabled` | content / tooling TAA block | enabled | false |
| Other `view.taa.*` bindings | content / tooling TAA block | corresponding TAA field | - |
| `view.gtao.enabled`, `view.gtao.radius` | content / tooling GTAO block | - | - |
| `view.gtao.direction_count`, `step_count`, `temporal_max_samples` | content / tooling GTAO block | corresponding GTAO field | - |
| `view.gtao.temporal_accumulation` | content / tooling GTAO block | temporal accumulation | false |
| `view.exposure.ev100`, `view.exposure.compensation` | camera | - | - |

The sequence patches call Runtime's typed patch functions from
[FrameSequenceCoordinator.cpp](../../Sources/GGLabAppRuntime/Capture/FrameSequenceCoordinator.cpp).
A sequence camera or reference view can change exposure only by changing the camera.

### Discovery and tracking

`SessionParameterControl::DescribeHostView` resolves the next display view before
sequence/reference patches. Discovery exposes inherited and Host-resolved values,
override presence and attribution; `status.frame` exposes a frame's own projection.

Tracking is lazy: the composed Session control starts disabled, `parameters` and
parameter mutations enable it through AppRuntime, and unfinished captures or an
active sequence enable it in `TickFrame`. `RefreshSources` compares effective Host
values and Lab revisions; content binding and committed batches also advance the
parameter revision used by effects and capture settling. `hostViewRevision` is
maintained independently.

## Content activation and continuation

[SynchronizeContentBinding](../../Sources/GGLabAppRuntime/GGLabAppRuntime.cpp)
uses the DemoManager's active-content identity, which the temporal plan also uses.
Binding synchronization runs before and after content update, before checking RHI
frame readiness.

`SessionParameterControl::SynchronizeContent` keeps Host overrides only when a
non-terminal batch is waiting for content, the Demo and LabRuntime owners match,
and an owning source's `ContinuesContentChange` accepts the old/new identities.
That is a session-owned Rebuild batch; a rebuild started any other way, or a
same-Demo Lab switch, does not qualify. Continuation rebinds the Lab and profile
sources with `continueView: true`, skipping the Host view clear. Otherwise old
batches are superseded, replacement sources are bound and Host overrides are
cleared. Release and shutdown also clear them while the old content owner is alive.

## Frame evidence

`BuildCaptureTemporalState` and `BuildCaptureFrameState` in `TickFrame` produce the
state used at capture issue. Their fields map to
[FrameCaptureMetadata.h](../../Sources/GGLabAppRuntime/Capture/FrameCaptureMetadata.h)
and its [serializer](../../Sources/GGLabAppRuntime/Capture/FrameCaptureMetadata.cpp):

| Observation | Evidence at issue time |
| --- | --- |
| Run identity | Created once by `GGLabAppRuntime::InitializeServices`, passed to the capture coordinator, returned by `GetRunIdentity` and serialized as `environment.process.runIdentity` |
| Host view control stamp | `m_HostViewControl`: run identity, content identity and `hostViewRevision` |
| Host view values and sources | `m_HostViewProjection`, after sequence or reference patches |
| Frame outcome | `m_Outcome`: planned, then submitted (`OnFrameSubmitted`) or aborted (`OnFrameAborted`) |
| Content activation | `m_Temporal.m_SessionIdentity` from `m_ActiveContentIdentity`; textual `m_LabId`/`m_DemoId` |
| Parameter revision | `m_ParameterRevision`, used for effect identity and capture settling |
| TAA enabled | `m_Temporal.m_Requested` -> `temporal.requested`, after reference masking |
| TAA settings | `m_Temporal` settings -> `temporal.settings.*` |
| GTAO settings | `m_Temporal.m_GTAO` -> `temporal.gtao.*` |
| Exposure bindings | The issue-time Host projection includes both manual inputs; `SetFrameSceneInputs` adds the built `sceneInputs.exposure.*` record |
| Consumer status/reason | `m_Temporal.m_Consumers` -> `temporal.consumers[]`, named `temporal-aa`, `reference`, `temporal-gtao` |
| Camera, extent and time | `m_Camera`, `m_Temporal` extents, `m_FixedDeltaTime`, `m_TotalTime` |

`status.frame` reports the stamp, projection and frame outcome. At issue,
`FrameCaptureCoordinator::Issue` copies the stamp and projection into
`FrameCaptureMetadata::m_Control`. Its versioned `control` record is saved in image
sidecars, graph/resource frame metadata and bundle provenance, alongside the plan's
predicate when present.

### Capture predicates

[TrySubmitCaptureJob](../../Sources/GGLabAppRuntime/GGLabAppRuntimeServices.cpp)
checks the required content activation and committed Host revision;
[CheckJobAdmission](../../Sources/GGLabAppRuntime/Capture/FrameCaptureCoordinator.cpp)
checks the plan, run and
[typed predicate](../../Sources/GGLabAppRuntime/Capture/RequiredFrameState.cpp).
Admission copies the predicate into private immutable storage. `BeginFrame`
evaluates it before capture issue: a lower revision waits, a matching due frame
issues, and a contradiction fails the job. A retry evaluates the same predicate
on its new frame; an issued attempt retains its frame's evidence through readback.

`Application::HandleControlRequest` calls
[CheckApplicationControlRunIdentity](../../Sources/WinAppHost/Application/Control/ApplicationControlProtocol.cpp)
before any handle lookup or admission. A request whose optional `expectedRunIdentity`
names another run is rejected, so a reused session name cannot reach the new
process's job or batch IDs. The session client binds override operations and
predicate captures to their specified run, including result polling.

After issue, `OnFrameBuilt` checks parameter effects against the built display view,
`SetFrameSceneInputs` adds exposure and lighting inputs, and only a successful
`EndFrame` lets `OnFrameSubmitted` record consumption. Full
[graph exports](../../Sources/GGLabRuntime/Private/Diagnostics/Builders/RenderGraphSnapshotJson.cpp)
list culled passes with `compile.passes[].culled` and record
`execution.status: recorded` with `execution.executedPassIndices`; summary exports
omit culled passes and the execution record. The Temporal AA resolve pass is
`PostProcess.TemporalAA`, declared in
[RenderPassTemporalAA.h](../../Sources/GGLabRuntime/Private/Graphics/RenderPass/RenderPassTemporalAA.h).

## Owning tests

| Target / source | Current coverage |
| --- | --- |
| `GGLabAppRuntimeTests`: [ParameterTransactionSelfTests.cpp](../../Tests/GGLabAppRuntime/ParameterTransactionSelfTests.cpp) | Batches, receipts, inherited parameters, `RunHostViewProjectionTests` and `RunHostViewAttributionTests` |
| `GGLabAppRuntimeTests`: [GpuFaultLifecycleSelfTests.cpp](../../Tests/GGLabAppRuntime/GpuFaultLifecycleSelfTests.cpp) | Effective settings, Host view control state, exact Host view override states, canonical request ownership/rejection, protected Lab/profile conflicts and Deferred/Rebuild lifecycle, sequence-only coercion and read-only request projection, content binding, parameter-frame and sequence-event scenarios |
| `GGLabAppRuntimeTests`: [FrameCaptureCoordinatorSelfTests.cpp](../../Tests/GGLabAppRuntime/FrameCaptureCoordinatorSelfTests.cpp) | Capture admission, predicates and immutable snapshots, readiness/settling, retries, SameFrame provenance and cancellation |
| `WinAppTests`: [ApplicationControlSelfTests.cpp](../../Tests/WinApp/ApplicationControlSelfTests.cpp), [ApplicationLifecycleSelfTests.cpp](../../Tests/WinApp/ApplicationLifecycleSelfTests.cpp), [DevToolsViewProfileSelfTests.cpp](../../Tests/WinApp/DevToolsViewProfileSelfTests.cpp) | Wire parsing, run checks, exact-state operations and their results, status control state, host lifecycle and DevTools view-profile overrides |
| `GGLabRuntimeTests`: [RenderingContractSelfTests.cpp](../../Tests/GGLabRuntime/RenderingContractSelfTests.cpp), [DiagnosticsContractSelfTests.cpp](../../Tests/GGLabRuntime/DiagnosticsContractSelfTests.cpp) | View/temporal contracts, environment control ownership and direct World authoring revisions |
| [TestCompareCaptures.ps1](../../Tests/Verification/TestCompareCaptures.ps1), suite `capture-comparison` | Capture comparison and its input checks |
| [TestTaaComparisonWorkflow.ps1](../../Tests/Verification/TestTaaComparisonWorkflow.ps1), suite `taa-comparison-workflow` | Guarded experiment recovery, cancellation verdicts, uncertain responses, exact restoration, process ownership and full-graph execution evidence |
| [Invoke-TaaComparison.ps1](../../Scripts/Verification/Invoke-TaaComparison.ps1), native suite `taa-comparison` | Static Native-pinned TAA A/B orchestration, comparison policy, SameFrame bundle validation and submitted restoration proof; operational syntax in [captures](../../.agents/skills/gglab-frame-capture/references/captures.md#guarded-taa-comparison) |
| [TestVerificationSupport.ps1](../../Tests/Verification/TestVerificationSupport.ps1), suite `verification-support` | Session client run binding, result polling and input errors |
| [Capture bundle](../../Scripts/Verification/ValidateCaptureBundles.py), [graph](../../Scripts/Verification/ValidateRenderGraphExports.py) and [parameter-frame](../../Scripts/Verification/ValidateParameterFrames.py) validators | Persisted schemas and provenance |
| Native suites in [RunVerification.ps1](../../Tests/Verification/RunVerification.ps1) | `protected-assignments`, `session-host-parameters`, `session-parameters`, `sequence-parameter-events`, `session-rebuild-parameters`, `capture-jobs` |

`GGLabAppRuntimeTests` runs as a whole executable without a suite selector. Windows
host cases belong in `WinAppTests`; the product executable has no regression entry.

```text
Anchors:
  - Sources/GGLabAppRuntime/Parameters/HostViewParameterSource.cpp::FinalizeEffects
  - Sources/GGLabAppRuntime/Parameters/HostViewParameterSource.cpp::PrepareOverrides
  - Sources/GGLabAppRuntime/Parameters/HostViewParameters.cpp::DescribeHostViewProjection
  - Sources/GGLabAppRuntime/Parameters/SessionParameterControl.cpp::SynchronizeContent
  - Sources/GGLabAppRuntime/Parameters/SessionParameterControl.cpp::AdvanceHostViewRevision
  - Sources/GGLabAppRuntime/Parameters/SessionParameterControl.cpp::AdmitParameterRequest
  - Sources/GGLabAppRuntime/Lab/LabParameter.cpp::AdvanceControlRevision
  - Sources/GGLabRuntime/Public/GGLabRuntime/Core/World.h::MarkLightingAuthoringChanged
  - Sources/GGLabRuntime/Private/Graphics/EnvironmentLightingSystem.cpp::RecordSettingsWrite
  - Sources/GGLabAppRuntime/ApplicationToolingIntegration.cpp::ResolveApplicationViewSettings
  - Sources/GGLabAppRuntime/GGLabAppRuntimeFrame.cpp::BuildCaptureTemporalState
  - Sources/GGLabAppRuntime/Capture/RequiredFrameState.cpp::ValidateRequiredFrameState
  - Sources/GGLabAppRuntime/Capture/FrameCaptureCoordinator.cpp::FailRequiredFrameStateMismatch
  - Sources/GGLabRuntime/Private/Graphics/RenderPass/RenderPassTemporalAA.h
  - Sources/WinAppHost/Application/Control/ApplicationControlProtocol.cpp::CheckApplicationControlRunIdentity
  - Scripts/Verification/CompareCaptures.ps1
  - Scripts/Verification/Invoke-TaaComparison.ps1
  - Scripts/Verification/TaaComparison.policy.json
ReviewPaths:
  - Sources/GGLabAppRuntime/Parameters/*
  - Sources/GGLabAppRuntime/Lab/LabParameter.*
  - Sources/GGLabRuntime/Public/GGLabRuntime/Core/World.h
  - Sources/GGLabRuntime/Private/Diagnostics/DirectionalLightTooling.cpp
  - Sources/GGLabRuntime/Private/Diagnostics/WorldTooling.cpp
  - Sources/GGLabRuntime/Private/Graphics/EnvironmentLightingSystem.*
  - Sources/GGLabAppRuntime/Capture/CapturePlan.h
  - Sources/GGLabAppRuntime/Capture/RequiredFrameState.*
  - Sources/GGLabAppRuntime/Capture/FrameCaptureCoordinator.*
  - Sources/GGLabAppRuntime/Capture/FrameCaptureMetadata.*
  - Sources/GGLabAppRuntime/Capture/FrameSequenceCoordinator.*
  - Sources/GGLabAppRuntime/Capture/CaptureBundle.cpp
  - Sources/GGLabAppRuntime/ApplicationToolingIntegration.*
  - Sources/GGLabAppRuntime/GGLabAppRuntime*.cpp
  - Sources/GGLabRuntime/Private/Graphics/ViewRenderSettings*.cpp
  - Sources/GGLabRuntime/Private/Graphics/RenderFrameBuilder.cpp
  - Sources/GGLabRuntime/Public/GGLabRuntime/Graphics/Pipeline/TemporalFramePlan.h
  - Sources/GGLabRuntime/Private/Diagnostics/Builders/RenderGraphSnapshotJson.cpp
  - Sources/WinAppHost/Application/Control/ApplicationControlParameters.cpp
  - Sources/WinAppHost/Application/Control/ApplicationControlProtocol.cpp
  - Sources/WinAppHost/DevTools/DevToolsRuntime.*
  - Scripts/Verification/GGLabSession.ps1
  - Projects/GGLabAppRuntime/GGLabAppRuntime.vcxproj
  - Projects/WinAppHost/WinAppHost.vcxproj
  - Projects/GGLabRuntime/GGLabRuntime.vcxproj
  - Tests/GGLabAppRuntime/ParameterTransactionSelfTests.cpp
  - Tests/GGLabAppRuntime/GpuFaultLifecycleSelfTests.cpp
  - Tests/GGLabAppRuntime/FrameCaptureCoordinatorSelfTests.cpp
  - Tests/WinApp/ApplicationControlSelfTests.cpp
  - Tests/WinApp/DevToolsViewProfileSelfTests.cpp
  - Tests/Verification/TestHostParameters.ps1
  - Tests/Verification/TestProtectedAssignments.ps1
  - Tests/Verification/TestVerificationSupport.ps1
  - Scripts/Verification/ValidateCaptureBundles.py
  - Tests/Verification/TestCompareCaptures.ps1
```
