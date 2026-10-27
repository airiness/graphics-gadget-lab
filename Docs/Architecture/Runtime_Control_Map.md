# Runtime Control and Frame Evidence
Atlas ID: atlas.application.runtime-control

- VerifiedAgainst: `811373cb8ee28874fe4101bf5e33c01b3237b47d`

This map connects session requests, resolved display settings and captured-frame
state. Frame execution and settlement are in the [frame map](Frame_Execution_Map.md).

## Owners and entry points

| Owner | Entry points | Responsibility |
| --- | --- | --- |
| `WinAppHost` | [Control adapter](../../Sources/WinAppHost/Application/Control/ApplicationControlParameters.cpp), [DevTools](../../Sources/WinAppHost/DevTools/DevToolsRuntime.cpp) | Named-pipe requests and GUI tooling settings. |
| `GGLabAppRuntime` | [AppRuntime interface](../../Sources/GGLabAppRuntime/GGLabAppRuntime.h), [SessionParameterControl](../../Sources/GGLabAppRuntime/Parameters/SessionParameterControl.h) | Run identity, request admission, parameter transactions, overrides and receipts. |
| `GGLabAppRuntime` | [TickFrame](../../Sources/GGLabAppRuntime/GGLabAppRuntimeFrame.cpp), [capture coordinator](../../Sources/GGLabAppRuntime/Capture/FrameCaptureCoordinator.h) | Resolve display state, apply sequence/reference masks and bind capture jobs to frames. |
| `GGLabRuntime` | [View settings](../../Sources/GGLabRuntime/Public/GGLabRuntime/Graphics/ViewRenderSettings.h), [typed patches](../../Sources/GGLabRuntime/Public/GGLabRuntime/Graphics/ViewRenderSettingsPatch.h), [temporal plan](../../Sources/GGLabRuntime/Public/GGLabRuntime/Graphics/Pipeline/TemporalFramePlan.h) | Normalize rendering values and resolve temporal consumers. |

## Session servicing

The pipe server wakes the platform owner when a request is queued. Application
services queries, cancellation and stop while AppRuntime is Suspended, without
rendering a frame. Its platform wait observes window events, automation idle
expiry and the earliest capture selection deadline. Developer sessions have no
idle expiry. AppRuntime exposes frame-free deadline servicing through
`GetNextControlDeadline` and `ServiceControlDeadlines` in
[runtime services](../../Sources/GGLabAppRuntime/GGLabAppRuntimeServices.cpp).

A predicate capture keeps one absolute admission deadline across readiness,
settling, capacity waits and retries. An issued frame settles first; successful
submission ends the deadline. Expiry returns `FrameSelectionExpired`, releases
active-job capacity and retains the receipt until consumption. Issued Runtime work
keeps its resources until drained. Commands and timeout options are described in
[session jobs](../../.agents/skills/gglab-frame-capture/references/session-jobs.md).

## Host override writers

[ParameterRequest.h](../../Sources/GGLabAppRuntime/Parameters/ParameterRequest.h)
defines Assign, Pin and RemoveOverride. WinAppHost and scheduled parameter events
submit these requests through AppRuntime to `SessionParameterControl`, which owns
admission, safe application and result retention. External requests share sequence
exclusion and one retained result slot.

[HostViewParameterSource](../../Sources/GGLabAppRuntime/Parameters/HostViewParameterSource.cpp)
owns Host view override storage. Prepared writes commit at `OnFrameReady`.
Ordinary assignments remove newly introduced overrides equal to inherited values;
exact-state requests preserve explicit pins and removals. Their control commit is
reported separately from rendered effects.

`hostViewRevision` advances when a committed operation changes the override set or
content binding clears it. Queries and inherited-value changes leave it unchanged.
Exact operations check run identity, content identity and this revision before
admission and again at commit. See
[exact override states](../../.agents/skills/gglab-frame-capture/references/session-parameters.md#guarded-exact-override-states)
for the wire contract.

GUI settings have separate storage in `DevToolsRuntime`; AppRuntime snapshots
active tooling blocks once per tick. Sequence settings patch frame-local values.
The Deferred `environment.profile` source and Lab authoring affect inherited values.

### GUI observation

AppRuntime supplies an owned, read-only
[ViewControlSnapshot](../../Sources/GGLabAppRuntime/Parameters/ViewControlSnapshot.h)
for each tooling Draw. It separates inherited values/sources, exact Host override
presence/value, Host-resolved values and the current frame plan after masks.
The previous frame retains its own control stamp, outcome and temporal consumers.
Observation leaves parameter tracking and revisions unchanged.

[View control widgets](../../Sources/WinAppHost/DevTools/DevelopGui/ViewControlWidgets.cpp)
expose TAA/GTAO and display-view exposure provenance and masking. The Camera
Inspector distinguishes the inspected camera from the display view.

[ToolingViewControl](../../Sources/GGLabAppRuntime/Parameters/ViewControlActions.h)
submits single-field removals using the displayed control stamp through
AppRuntime's admission path. Session owns receipts; AppRuntime collects its
tooling-owned terminal results even when the panel is closed.

## Protected Lab/profile assignments

`assign` carries a single-domain request with `ParameterRequest::m_ExpectedDomain`.
AppRuntime checks the run; Session control checks the domain stamp at admission
and safe commit. The stamp covers Session binding, Lab owner/value revisions,
[World lighting authoring](../../Sources/GGLabRuntime/Public/GGLabRuntime/Core/World.h)
and [environment intent](../../Sources/GGLabRuntime/Private/Graphics/EnvironmentLightingSystem.cpp).
Environment bake publication has a separate lifecycle.

Control conflicts and commits are recorded in `m_DomainOperation`; effect receipts
track application and submitted-frame consumption. Rebuilds use the
[content continuation](#content-activation-and-continuation) path. Client usage is in
[protected assignments](../../.agents/skills/gglab-frame-capture/references/session-parameters.md#protected-labprofile-assignment).

## Display-view resolution

[ResolveApplicationViewSettings](../../Sources/GGLabAppRuntime/ApplicationToolingIntegration.cpp)
combines the active content's `ViewRenderProfile`, whole-block tooling overrides
and the selected camera's exposure inputs, then calls Runtime's
[ResolveViewRenderSettings](../../Sources/GGLabRuntime/Private/Graphics/ViewRenderSettings.cpp).
Host overrides apply to this effective display view; other views resolve separately.

```text
content update and safe parameter application
  -> reference-view restoration / sequence camera pose
  -> select display view
  -> authored profile + tooling blocks + camera exposure
  -> Host view overrides
  -> reference sampling masks OR sequence settings patches
  -> temporal plan -> capture issue
  -> BuildFrame -> graph recording -> EndFrame -> submitted feedback
```

`Tick` captures tooling before `TickFrame`, so GUI edits made while drawing enter a
later tick. [RenderFrameBuilder](../../Sources/GGLabRuntime/Private/Graphics/RenderFrameBuilder.cpp)
uses the resolved display settings for the selected main/debug view.

### Host view bindings and the layers that patch them

[HostViewParameters.cpp](../../Sources/GGLabAppRuntime/Parameters/HostViewParameters.cpp)
owns binding IDs, types and ranges. `DescribeHostViewProjection` attributes values
to the last writer: inherited `content`, `tooling` or `camera`, then `host`, then
`sequence` or `forced-reference`. Attribution follows writes even for equal values.
Reference sampling and sequence settings patches are mutually exclusive.

### Discovery and tracking

`DescribeHostView` reports inherited and Host-resolved values before sequence or
reference masks. `status.frame` reports the latest frame's projection and outcome.
Parameter discovery, mutations, captures and sequences enable lazy tracking.
The parameter revision tracks effective changes for effects and settling;
`hostViewRevision` tracks override structure independently.

## Content activation and continuation

[SynchronizeContentBinding](../../Sources/GGLabAppRuntime/GGLabAppRuntime.cpp)
uses DemoManager's active-content identity before and after content update.
Replacement and shutdown clear Host overrides while their old owner is alive.

A Session-owned Rebuild can retain them when a non-terminal batch waits for
content, the Demo and LabRuntime owners match, and its source accepts the new
identity through `ContinuesContentChange`. Continuation rebinds Lab/profile
sources with `continueView: true`. Other replacements supersede old batches and
clear the override set.

## Frame evidence

`BuildCaptureTemporalState` and `BuildCaptureFrameState` in `TickFrame` produce the
issue-time snapshot. [FrameCaptureMetadata](../../Sources/GGLabAppRuntime/Capture/FrameCaptureMetadata.h)
and its [serializer](../../Sources/GGLabAppRuntime/Capture/FrameCaptureMetadata.cpp)
carry that snapshot into image sidecars, graph/resource metadata and bundle provenance.

| State | Frame binding |
| --- | --- |
| Control identity | Process run identity, content activation and `hostViewRevision`. |
| Resolved view | Host projection, value sources, temporal settings and consumer status after masks. |
| Frame | Serial, outcome, camera, extents, time and parameter revision. |
| Built scene | Exposure and lighting inputs appended by `SetFrameSceneInputs`. |

### Capture predicates

[TrySubmitCaptureJob](../../Sources/GGLabAppRuntime/GGLabAppRuntimeServices.cpp)
and the [coordinator](../../Sources/GGLabAppRuntime/Capture/FrameCaptureCoordinator.cpp)
validate the plan and [typed predicate](../../Sources/GGLabAppRuntime/Capture/RequiredFrameState.cpp),
then retain an immutable copy. At issue, a lower revision waits, a matching due
frame issues and a contradiction fails. Retries evaluate the same predicate on
new frames; issued attempts retain their own snapshot through readback.

[Run checks](../../Sources/WinAppHost/Application/Control/ApplicationControlProtocol.cpp)
precede admission and handle lookup, keeping job and batch IDs scoped to their
process. `OnFrameBuilt` checks effects against the built view; `OnFrameSubmitted`
records consumption only after a successful EndFrame.

Full [graph exports](../../Sources/GGLabRuntime/Private/Diagnostics/Builders/RenderGraphSnapshotJson.cpp)
record culled passes and executed pass indices. The TAA resolve pass is
`PostProcess.TemporalAA`. The [TAA comparison workflow](../../Scripts/Verification/Invoke-TaaComparison.ps1)
pairs same-frame images and graphs, checks execution and restores the original
Host override state. Operational details belong to the
[capture guide](../../.agents/skills/gglab-frame-capture/references/captures.md#guarded-taa-comparison).

## Owning tests

| Target / entry | Coverage |
| --- | --- |
| `GGLabAppRuntimeTests`: [parameter transactions](../../Tests/GGLabAppRuntime/ParameterTransactionSelfTests.cpp), [fault lifecycle](../../Tests/GGLabAppRuntime/GpuFaultLifecycleSelfTests.cpp) | Transactions, control identity, binding, continuation and submitted effects. |
| `GGLabAppRuntimeTests`: [capture coordinator](../../Tests/GGLabAppRuntime/FrameCaptureCoordinatorSelfTests.cpp) | Admission, predicates, deadlines, retries, cancellation and same-frame provenance. |
| `WinAppTests`: [control](../../Tests/WinApp/ApplicationControlSelfTests.cpp), [lifecycle](../../Tests/WinApp/ApplicationLifecycleSelfTests.cpp), [tooling](../../Tests/WinApp/DevToolsViewProfileSelfTests.cpp) | Wire contracts, suspended sessions and tooling overrides. |
| `GGLabRuntimeTests`: [rendering](../../Tests/GGLabRuntime/RenderingContractSelfTests.cpp), [diagnostics](../../Tests/GGLabRuntime/DiagnosticsContractSelfTests.cpp) | View/temporal contracts and lighting authoring identity. |
| [Verification catalog](../../Tests/Verification/RunVerification.ps1) | Session, parameter, capture-schema and TAA comparison suites; use `-List` for selection. |

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
  - Sources/GGLabAppRuntime/Parameters/ViewControlSnapshot.cpp::BuildViewControlSnapshot
  - Sources/GGLabAppRuntime/Parameters/ViewControlActions.cpp::ToolingViewControl::ReleaseOverride
  - Sources/WinAppHost/DevTools/DevelopGui/ViewControlWidgets.cpp::DrawViewControlSnapshot
  - Sources/GGLabAppRuntime/GGLabAppRuntimeFrame.cpp::BuildCaptureTemporalState
  - Sources/GGLabAppRuntime/Capture/RequiredFrameState.cpp::ValidateRequiredFrameState
  - Sources/GGLabAppRuntime/Capture/FrameCaptureCoordinator.cpp::FailRequiredFrameStateMismatch
  - Sources/GGLabRuntime/Private/Graphics/RenderPass/RenderPassTemporalAA.h
  - Sources/WinAppHost/Application/Control/ApplicationControlProtocol.cpp::CheckApplicationControlRunIdentity
  - Scripts/Verification/CompareCaptures.ps1
  - Scripts/Verification/Invoke-TaaComparison.ps1
  - Scripts/Verification/TaaComparison.policy.json
  - Sources/GGLabAppRuntime/GGLabAppRuntimeServices.cpp::ServiceControlDeadlines
  - Sources/WinAppHost/Application/Application.cpp::WaitWhileSuspended
  - Sources/WinAppHost/Application/Platform/Windows/Win32PlatformHost.cpp::WaitForEvents
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
  - Sources/WinAppHost/DevTools/DevelopGui/DevelopGuiApplicationTooling.cpp
  - Sources/WinAppHost/DevTools/DevelopGui/DevelopGuiContext.h
  - Sources/WinAppHost/DevTools/DevelopGui/Panels/TemporalAAInspectorPanel.cpp
  - Sources/WinAppHost/DevTools/DevelopGui/Panels/GTAOInspectorPanel.cpp
  - Sources/WinAppHost/DevTools/DevelopGui/Panels/PostProcessInspectorPanel.cpp
  - Sources/WinAppHost/DevTools/DevelopGui/Panels/CameraInspectorPanel.cpp
  - Sources/WinAppHost/DevTools/DevelopGui/ViewControlWidgets.*
  - Sources/GGLabAppRuntime/GGLabAppRuntime*.cpp
  - Sources/GGLabRuntime/Private/Graphics/ViewRenderSettings*.cpp
  - Sources/GGLabRuntime/Private/Graphics/RenderFrameBuilder.cpp
  - Sources/GGLabRuntime/Public/GGLabRuntime/Graphics/Pipeline/TemporalFramePlan.h
  - Sources/GGLabRuntime/Private/Diagnostics/Builders/RenderGraphSnapshotJson.cpp
  - Sources/WinAppHost/Application/Control/ApplicationControlParameters.cpp
  - Sources/WinAppHost/Application/Control/ApplicationControlProtocol.cpp
  - Sources/WinAppHost/DevTools/DevToolsRuntime.*
  - Sources/WinAppHost/Application/Platform/Windows/Win32NamedPipeServer.*
  - Sources/WinAppHost/Application/Platform/Windows/Win32PlatformHost.*
  - Scripts/Verification/GGLabSession.ps1
  - Projects/GGLabAppRuntime/GGLabAppRuntime.vcxproj
  - Projects/WinAppHost/WinAppHost.vcxproj
  - Projects/GGLabRuntime/GGLabRuntime.vcxproj
  - Tests/GGLabAppRuntime/ParameterTransactionSelfTests.cpp
  - Tests/GGLabAppRuntime/GpuFaultLifecycleSelfTests.cpp
  - Tests/GGLabAppRuntime/FrameCaptureCoordinatorSelfTests.cpp
  - Tests/WinApp/ApplicationControlSelfTests.cpp
  - Tests/WinApp/ApplicationLifecycleSelfTests.cpp
  - Tests/WinApp/DevToolsViewProfileSelfTests.cpp
  - Tests/Verification/TestHostParameters.ps1
  - Tests/Verification/TestSessionLifecycle.ps1
  - Tests/Verification/TestProtectedAssignments.ps1
  - Tests/Verification/TestVerificationSupport.ps1
  - Scripts/Verification/ValidateCaptureBundles.py
  - Tests/Verification/TestCompareCaptures.ps1
```
