# Architecture task routing
Atlas ID: atlas.routes.frame-settlement

- Question: where should a frame-settlement investigation or change begin?
- VerifiedAgainst: `b89e482e8f7cd7c2ed092ee1e55cdfb6a9a5f624`
- Refresh triggers: public interfaces, settlement ownership, caller feedback,
  retention rules or owning verification change.

Select one row, then follow its source and contract links. Cards describe the
provider/consumer and lifetime details; the table selects the starting point.

| Task or question | Owning project and source entry | Map/card and governing local contract | Owning verification |
| --- | --- | --- | --- |
| Find the Public rendering interface and its implementation owner | `GGLabRuntime`: [RenderHost.h](../../../../Sources/GGLabRuntime/Public/GGLabRuntime/Graphics/RenderHost.h) → [factory](../../../../Sources/GGLabRuntime/Private/Graphics/RenderHostFactory.cpp) | [Render host card](../../../../Docs/Architecture/Frame_Execution_Map.md#render-host-and-settlement); [dependency rules](../../../../AGENTS.md#ownership-and-dependency-boundaries) | Public/Private project visibility: [boundary validator](../../../../Scripts/ValidateProjectBoundaries.ps1) |
| Explain recorded work that did not complete successfully | `GGLabRuntime`: [outcome derivation](../../../../Sources/GGLabRuntime/Private/Graphics/RenderFrameOutcome.h) → [Renderer::SettleFrame](../../../../Sources/GGLabRuntime/Private/Graphics/Renderer.cpp) | [outcomes and host reaction](../../../../Docs/Architecture/Frame_Execution_Map.md#early-exit-and-outcome-derivation); [Frame settlement](../../../../Docs/Architecture/Frame_Execution_Map.md#frame-settlement) | [Rendering contracts](../../../../Tests/GGLabRuntime/RenderingContractSelfTests.cpp) and [fault lifecycle](../../../../Tests/GGLabAppRuntime/GpuFaultLifecycleSelfTests.cpp) |
| Change deferred CPU publication or handle a late enqueue | `GGLabRuntime`: [RGDeferredCommit.h](../../../../Sources/GGLabRuntime/Public/GGLabRuntime/Graphics/RenderGraph/RGDeferredCommit.h) → [CommitAll](../../../../Sources/GGLabRuntime/Private/Graphics/RenderGraph/RGDeferredCommit.cpp) → Renderer EndFrame; `GGLabAppRuntime`: [TickFrame](../../../../Sources/GGLabAppRuntime/GGLabAppRuntimeFrame.cpp) | [graph card](../../../../Docs/Architecture/Frame_Execution_Map.md#rendergraph-execution-and-deferred-cpu-commits); [Settlement contract failures](../../../../Docs/Architecture/Frame_Execution_Map.md#settlement-contract-failures) | Runtime `rendering-contracts`; AppRuntime `gpu-fault-lifecycle`, including `RunLateCommitScenario` |
| Follow GPU-visible retention and shutdown | `GGLabAppRuntime`: [Shutdown](../../../../Sources/GGLabAppRuntime/GGLabAppRuntimeServices.cpp) → `GGLabRuntime`: [QuiesceRHIForTeardown](../../../../Sources/GGLabRuntime/Private/Graphics/RHI/RHIContext.cpp) → Renderer Finalize | [RHI card](../../../../Docs/Architecture/Frame_Execution_Map.md#rhi-submission-and-resource-retirement); [Teardown ownership](../../../../Docs/Architecture/Frame_Execution_Map.md#teardown-ownership); [checked completion interface](../../../../Sources/GGLabRuntime/Public/GGLabRuntime/Graphics/RHI/RHIDevice.h) | AppRuntime `gpu-fault-lifecycle`; Runtime `rendering-contracts` |
| Review an affected map or a dirty source overlay | Change author: [anchor/impact helper](../../../../Scripts/CheckArchitectureAtlas.ps1), then the reported entry's source; subsystem owner reviews contract changes | [refresh procedure](../../../../Docs/Architecture/Boundary_And_Drift_Guide.md#refresh-one-affected-entry) and [pending refreshes](../../../../Docs/Architecture/Boundary_And_Drift_Guide.md#pending-refreshes) | Helper `-SelfTest`, navigation and source/contract comparison; boundary validation remains separate |

Suite registration and CLI selection are in the [Runtime test main](../../../../Tests/GGLabRuntime/GGLabRuntimeTestsMain.cpp)
and [AppRuntime test main](../../../../Tests/GGLabAppRuntime/GGLabAppRuntimeLifecycleSelfTests.cpp).
GPU capture and visual checks use [gglab-frame-capture](../../gglab-frame-capture/SKILL.md)
when needed.
RenderPass authoring and Lab-switch routes will be added with their maps.

```text
Anchors:
  - Sources/GGLabRuntime/Public/GGLabRuntime/Graphics/RenderHost.h::CreateRenderHost
  - Sources/GGLabRuntime/Private/Graphics/Renderer.cpp::Renderer::SettleFrame
  - Sources/GGLabRuntime/Private/Graphics/RenderGraph/RGDeferredCommit.cpp::RGDeferredCommitList::CommitAll
  - Sources/GGLabAppRuntime/GGLabAppRuntimeFrame.cpp::TickFrame
  - Sources/GGLabAppRuntime/GGLabAppRuntimeServices.cpp::Shutdown
ReviewPaths:
  - Tests/GGLabRuntime/RenderingContractSelfTests.cpp
  - Tests/GGLabAppRuntime/GpuFaultLifecycleSelfTests.cpp
  - Docs/Architecture/Frame_Execution_Map.md
  - Docs/Architecture/Boundary_And_Drift_Guide.md
  - Scripts/CheckArchitectureAtlas.ps1
```
