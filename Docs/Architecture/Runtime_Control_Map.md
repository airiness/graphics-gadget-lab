# Runtime Control and Frame Evidence
Atlas ID: atlas.application.runtime-control

- Question: who writes Host view overrides, how is the display view resolved, and
  which existing evidence ties a captured frame to that state?
- VerifiedAgainst: `fbb5f36a71fbeacb1350ad68458f04e6c3a3b8cd`
- Refresh triggers: Host parameter bindings or writers, tooling snapshot inputs,
  content continuation, display-view precedence, capture issue order, provenance
  or owning regression targets change.

Frame stages after issue are in the [frame execution map](Frame_Execution_Map.md).

## Owners and entry points

| Owner | Interface and implementation | Responsibility |
| --- | --- | --- |
| `WinAppHost` | [ApplicationControlParameters.cpp](../../Sources/WinAppHost/Application/Control/ApplicationControlParameters.cpp), [DevToolsRuntime.cpp](../../Sources/WinAppHost/DevTools/DevToolsRuntime.cpp) | Wire adaptation and whole-block GUI tooling overrides; neither owns Host parameter receipts. |
| `GGLabAppRuntime` | [GGLabAppRuntime.h](../../Sources/GGLabAppRuntime/GGLabAppRuntime.h), [SessionParameterControl.h](../../Sources/GGLabAppRuntime/Parameters/SessionParameterControl.h), [source adapters](../../Sources/GGLabAppRuntime/Parameters/ParameterSourceAdapters.h) | Session admission, the process run identity, source binding, the private Host override set and its revision, transactions and effect receipts. |
| `GGLabAppRuntime` | [TickFrame](../../Sources/GGLabAppRuntime/GGLabAppRuntimeFrame.cpp), [CapturePlan.h](../../Sources/GGLabAppRuntime/Capture/CapturePlan.h), [FrameCaptureCoordinator.h](../../Sources/GGLabAppRuntime/Capture/FrameCaptureCoordinator.h) | Display-view planning, sequence masking, capture issue and job/publication coordination. |
| `GGLabRuntime` | Public [ViewRenderSettings.h](../../Sources/GGLabRuntime/Public/GGLabRuntime/Graphics/ViewRenderSettings.h), [ViewRenderSettingsPatch.h](../../Sources/GGLabRuntime/Public/GGLabRuntime/Graphics/ViewRenderSettingsPatch.h), [TemporalFramePlan.h](../../Sources/GGLabRuntime/Public/GGLabRuntime/Graphics/Pipeline/TemporalFramePlan.h) | Typed normalization, exposure derivation and temporal consumer/service resolution, independent of parameter strings or session handles. |

These owners are declared in the [AppRuntime](../../Projects/GGLabAppRuntime/GGLabAppRuntime.vcxproj),
[WinAppHost](../../Projects/WinAppHost/WinAppHost.vcxproj) and
[Runtime](../../Projects/GGLabRuntime/GGLabRuntime.vcxproj) projects.

## Host override writers

`SessionParameterControl::m_Sources` privately owns `ParameterSourceAdapters`, which
contains the production `HostViewParameterSource`; its `m_Overrides` vector is
private. Every production change to that set goes through Session control:

| Entry | Path to the Host override set |
| --- | --- |
| External `set` | WinAppHost parses values, then calls `GGLabAppRuntime::RequestParameterBatch`; AppRuntime checks sequence/external exclusion and delegates to `SessionParameterControl`. |
| Scheduled parameter event | `TickFrame` admits the event through the same `SessionParameterControl::RequestParameterBatch`, with its sequence owner ID. |
| Safe application | `OnFrameReady` calls `ApplyParameterBatch`; the Host prepared source stages a copy, `Commit` replaces `m_Overrides`, and `FinalizeEffects` removes newly introduced equal-value overrides. |
| Content replacement or release | `SynchronizeContent` calls `ParameterSourceAdapters::Bind`; without continuation it calls `HostViewParameterSource::Bind`, which clears `m_Overrides`. |
| Shutdown | `SessionParameterControl::Finish` ends batches and binds null sources, clearing the same set. |

The storage writes are in
[HostViewParameterSource.cpp](../../Sources/GGLabAppRuntime/Parameters/HostViewParameterSource.cpp);
the safe boundary and binding policy are in
[SessionParameterControl.cpp](../../Sources/GGLabAppRuntime/Parameters/SessionParameterControl.cpp).
`Refresh` rebuilds the discovery projection without changing override presence.

`SessionParameterControl` advances `hostViewRevision` at the same sites: once after
a committed batch whose finalized override set differs, and once when content
binding or shutdown clears a non-empty set. NoChange batches, inherited value
changes and queries leave it unchanged.

Two similarly named paths do **not** write this set:

- GUI panels edit `DevToolsRuntime::m_ViewRenderSettingsOverrides`; its
  `CaptureFrameSettings` copies active whole blocks into an AppRuntime snapshot.
- Legacy sequence TAA/GTAO overrides patch a frame-local settings value in
  `TickFrame`. Scheduled `view.*` parameter events, in contrast, use Session control.

`environment.profile` is a separate, Deferred Host source. Lab authoring writes can
change inherited values but never write Host view overrides.

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
blocks replaced authored settings. `m_Shadow` is not a view-settings input. `m_ScenePreExposure` changes the derived exposure storage
scale, not the two manual exposure inputs.

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
  -> reference sampling masks OR legacy sequence settings patches
  -> temporal consumer plan and transaction
  -> BuildCaptureFrameState -> FrameCaptureCoordinator::BeginFrame (issue)
  -> BuildFrame -> graph build/record -> EndFrame -> submitted feedback
```

`BuildFrame` copies `m_DisplayViewSettings` for the selected main/debug view in
[RenderFrameBuilder.cpp](../../Sources/GGLabRuntime/Private/Graphics/RenderFrameBuilder.cpp).
Other rendered views resolve separately and do not receive Host overrides.

### Host view bindings and the layers that patch them

[HostViewParameters.cpp](../../Sources/GGLabAppRuntime/Parameters/HostViewParameters.cpp)
declares 25 Immediate Host view bindings; types, ranges and enum members remain
authoritative there. `TAA` is `m_TemporalAA`, `GTAO` is `m_Lighting.m_GTAO` and
`Exposure` is `m_Exposure` in `ResolvedViewRenderSettings`. A dash means that layer
does not patch the binding. Reference sampling and legacy sequence patches are
mutually exclusive in a frame. `DescribeHostViewProjection` attributes each binding
to the last layer that wrote it: the inherited layer (`content`, `tooling` or
`camera`), then `host`, then `sequence` or `forced-reference`. Numeric equality
never changes the source.

| Host ID | Member | Inherited layer | Legacy sequence | Reference mask |
| --- | --- | --- | --- | --- |
| `view.taa.enabled` | `TAA.m_Enabled` | content / tooling TAA block | enabled | false |
| `view.taa.max_history_feedback` | `TAA.m_MaxHistoryFeedback` | content / tooling TAA block | same field | - |
| `view.taa.depth_absolute_threshold` | `TAA.m_DepthAbsoluteThreshold` | content / tooling TAA block | same field | - |
| `view.taa.depth_relative_threshold` | `TAA.m_DepthRelativeThreshold` | content / tooling TAA block | same field | - |
| `view.taa.velocity_weight_scale` | `TAA.m_VelocityWeightScale` | content / tooling TAA block | same field | - |
| `view.taa.luminance_weight_scale` | `TAA.m_LuminanceWeightScale` | content / tooling TAA block | same field | - |
| `view.taa.neighborhood_clamp_expansion` | `TAA.m_NeighborhoodClampExpansion` | content / tooling TAA block | same field | - |
| `view.taa.history_relaxation` | `TAA.m_HistoryRelaxation` | content / tooling TAA block | same field | - |
| `view.taa.variance_clip_gamma` | `TAA.m_VarianceClipGamma` | content / tooling TAA block | same field | - |
| `view.taa.texture_lod_bias_offset` | `TAA.m_TextureLodBiasOffset` | content / tooling TAA block | same field | - |
| `view.taa.history_accumulation` | `TAA.m_HistoryAccumulation` | content / tooling TAA block | same field | - |
| `view.taa.history_rectification` | `TAA.m_HistoryRectification` | content / tooling TAA block | same field | - |
| `view.taa.history_filter` | `TAA.m_HistoryFilter` | content / tooling TAA block | same field | - |
| `view.taa.current_filter` | `TAA.m_CurrentFilter` | content / tooling TAA block | same field | - |
| `view.taa.motion_selection` | `TAA.m_MotionSelection` | content / tooling TAA block | same field | - |
| `view.taa.post_temporal_view` | `TAA.m_PostTemporalView` | content / tooling TAA block | same field | - |
| `view.taa.resolution_preset` | `TAA.m_ResolutionPreset` | content / tooling TAA block | same field | - |
| `view.gtao.enabled` | `GTAO.m_Enabled` | content / tooling GTAO block | - | - |
| `view.gtao.radius` | `GTAO.m_Radius` | content / tooling GTAO block | - | - |
| `view.gtao.direction_count` | `GTAO.m_DirectionCount` | content / tooling GTAO block | same field | - |
| `view.gtao.step_count` | `GTAO.m_StepCount` | content / tooling GTAO block | same field | - |
| `view.gtao.temporal_max_samples` | `GTAO.m_TemporalMaxSamples` | content / tooling GTAO block | same field | - |
| `view.gtao.temporal_accumulation` | `GTAO.m_TemporalAccumulation` | content / tooling GTAO block | same field | false |
| `view.exposure.ev100` | `Exposure.m_ManualEV100` | camera | - | - |
| `view.exposure.compensation` | `Exposure.m_CompensationEV` | camera | - | - |

The sequence patches call Runtime's typed patch functions from
[FrameSequenceCoordinator.cpp](../../Sources/GGLabAppRuntime/Capture/FrameSequenceCoordinator.cpp).
A sequence camera or reference view can change exposure only by changing the camera.

### Discovery and tracking

Discovery `default` is the inherited resolved value; `current` includes Host
overrides but not sequence/reference masks; `source: host` names the parameter
owner. Discovery also reports `controlState` and, per Host view ID, `hostOverride`,
`resolved` and `inheritedSource` from `SessionParameterControl::DescribeHostView`:
the next frame's resolution before sequence or reference patches. Frozen parameter
values still hold only values that differ from their defaults, without override
presence or layer attribution.

Tracking is lazy: the composed Session control starts disabled, `parameters` and
`set` enable it through AppRuntime, and unfinished captures or an active sequence
enable it in `TickFrame`. `RefreshSources` compares effective Host values and Lab
revisions; content binding and committed batches also advance the legacy revision,
which capture settling uses. `hostViewRevision` does not depend on this tracking.

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
| Parameter revision | `m_ParameterRevision`, used only for settling |
| TAA enabled | `m_Temporal.m_Requested` -> `temporal.requested`, after reference masking |
| Other 16 TAA bindings | `m_Temporal` settings -> `temporal.settings.*` |
| Six GTAO bindings | `m_Temporal.m_GTAO` -> `temporal.gtao.*` |
| Exposure bindings | Final `displayViewSettings.m_Exposure` exists before issue but is persisted only after `BuildFrame`, as `sceneInputs.exposure.*` |
| Consumer status/reason | `m_Temporal.m_Consumers` -> `temporal.consumers[]`, named `temporal-aa`, `reference`, `temporal-gtao` |
| Camera, extent and time | `m_Camera`, `m_Temporal` extents, `m_FixedDeltaTime`, `m_TotalTime` |

`status.frame` reports the stamp, values, sources and outcome; capture sidecars do
not record them.

After issue, `OnFrameBuilt` checks parameter effects against the built display view,
`SetFrameSceneInputs` adds exposure and lighting inputs, and only a successful
`EndFrame` lets `OnFrameSubmitted` record consumption. Full
[graph exports](../../Sources/GGLabRuntime/Private/Diagnostics/Builders/RenderGraphSnapshotJson.cpp)
list culled passes with `compile.passes[].culled` and record
`execution.status: recorded` with `execution.executedPassIndices`; summary exports
omit culled passes and the execution record. The Temporal AA resolve pass is
`PostProcess.TemporalAA`, declared in
[RenderPassTemporalAA.h](../../Sources/GGLabRuntime/Private/Graphics/RenderPass/RenderPassTemporalAA.h).

[CompareCaptures.ps1](../../Scripts/Verification/CompareCaptures.ps1) reports
informational metadata differences with a tolerance; `-RequireMatchingInputs`
compares the same fields exactly, except `time.totalTime` and IBL publication
serials. Directory pairing keys include the capture label.

## Owning tests

| Target / source | Current coverage |
| --- | --- |
| `GGLabAppRuntimeTests`: [ParameterTransactionSelfTests.cpp](../../Tests/GGLabAppRuntime/ParameterTransactionSelfTests.cpp) | Batches, receipts, inherited parameters, `RunHostViewProjectionTests` and `RunHostViewAttributionTests` |
| `GGLabAppRuntimeTests`: [GpuFaultLifecycleSelfTests.cpp](../../Tests/GGLabAppRuntime/GpuFaultLifecycleSelfTests.cpp) | Effective settings, Host view control state, content binding, rebuild, parameter-frame and sequence-event scenarios |
| `GGLabAppRuntimeTests`: [FrameCaptureCoordinatorSelfTests.cpp](../../Tests/GGLabAppRuntime/FrameCaptureCoordinatorSelfTests.cpp) | Capture admission, readiness/settling, retries, SameFrame provenance and cancellation |
| `WinAppTests`: [ApplicationControlSelfTests.cpp](../../Tests/WinApp/ApplicationControlSelfTests.cpp), [ApplicationLifecycleSelfTests.cpp](../../Tests/WinApp/ApplicationLifecycleSelfTests.cpp), [DevToolsViewProfileSelfTests.cpp](../../Tests/WinApp/DevToolsViewProfileSelfTests.cpp) | Wire parsing, status control state, host lifecycle and DevTools view-profile overrides |
| `GGLabRuntimeTests`: [RenderingContractSelfTests.cpp](../../Tests/GGLabRuntime/RenderingContractSelfTests.cpp) | `RunViewSettingsPatchTests` and temporal history/reference contracts |
| [TestCompareCaptures.ps1](../../Tests/Verification/TestCompareCaptures.ps1), suite `capture-comparison` | Capture comparison and its input checks |
| [Capture bundle](../../Scripts/Verification/ValidateCaptureBundles.py), [graph](../../Scripts/Verification/ValidateRenderGraphExports.py) and [parameter-frame](../../Scripts/Verification/ValidateParameterFrames.py) validators | Persisted schemas and provenance |
| Native suites in [RunVerification.ps1](../../Tests/Verification/RunVerification.ps1) | `session-host-parameters`, `session-parameters`, `sequence-parameter-events`, `session-rebuild-parameters`, `capture-jobs` |

`GGLabAppRuntimeTests` runs as a whole executable without a suite selector. Windows
host cases belong in `WinAppTests`; the product executable has no regression entry.

```text
Anchors:
  - Sources/GGLabAppRuntime/Parameters/HostViewParameterSource.cpp::FinalizeEffects
  - Sources/GGLabAppRuntime/Parameters/HostViewParameters.cpp::DescribeHostViewProjection
  - Sources/GGLabAppRuntime/Parameters/SessionParameterControl.cpp::SynchronizeContent
  - Sources/GGLabAppRuntime/Parameters/SessionParameterControl.cpp::AdvanceHostViewRevision
  - Sources/GGLabAppRuntime/ApplicationToolingIntegration.cpp::ResolveApplicationViewSettings
  - Sources/GGLabAppRuntime/GGLabAppRuntimeFrame.cpp::BuildCaptureTemporalState
  - Sources/GGLabRuntime/Private/Graphics/RenderPass/RenderPassTemporalAA.h
  - Scripts/Verification/CompareCaptures.ps1
ReviewPaths:
  - Sources/GGLabAppRuntime/Parameters/*
  - Sources/GGLabAppRuntime/Capture/*
  - Sources/GGLabAppRuntime/ApplicationToolingIntegration.*
  - Sources/GGLabAppRuntime/GGLabAppRuntime*.cpp
  - Sources/GGLabRuntime/Private/Graphics/ViewRenderSettings*.cpp
  - Sources/GGLabRuntime/Private/Graphics/RenderFrameBuilder.cpp
  - Sources/GGLabRuntime/Public/GGLabRuntime/Graphics/Pipeline/TemporalFramePlan.h
  - Sources/GGLabRuntime/Private/Diagnostics/Builders/RenderGraphSnapshotJson.cpp
  - Sources/WinAppHost/Application/Control/ApplicationControlParameters.cpp
  - Sources/WinAppHost/DevTools/DevToolsRuntime.*
  - Projects/GGLabAppRuntime/GGLabAppRuntime.vcxproj
  - Projects/WinAppHost/WinAppHost.vcxproj
  - Projects/GGLabRuntime/GGLabRuntime.vcxproj
  - Tests/GGLabAppRuntime/*
  - Tests/Verification/TestCompareCaptures.ps1
```
