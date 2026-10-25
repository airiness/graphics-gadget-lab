# Architecture task routing

Select one row, then follow its source and contract links. Cards describe the
provider/consumer and lifetime details; the table selects the starting point.

## Project ownership and dependencies
Atlas ID: atlas.routes.project-ownership

- Question: which project owns the code and what build/include relationship
  applies to a change?
- VerifiedAgainst: `48f6226ef04ad528a744f3ae11933469a64b1627`
- Refresh triggers: source ownership, project references, imports, build profiles
  or privileged include visibility change.

| Task or question | Source entry | Map and governing local rule | Verification |
| --- | --- | --- | --- |
| Find a source owner or choose a project | [Solution](../../../../GraphicsGadgetLab.sln), then the owner's `ClCompile`/`ClInclude` items | [Project inventory](../../../../Docs/Architecture/Repository_Module_Map.md#projects-and-source-ownership); [ownership rules](../../../../AGENTS.md#ownership-and-dependency-boundaries) | [Boundary validator](../../../../Scripts/ValidateProjectBoundaries.ps1) |
| Change a project reference or build profile | The declaring `.vcxproj` and its imported `.props`/`.targets` | [Declared references](../../../../Docs/Architecture/Repository_Module_Map.md#declared-project-references) and [profiles](../../../../Docs/Architecture/Repository_Module_Map.md#build-profiles-and-imports) | Compare conditions and link/tool metadata; boundary checks |
| Follow a Public interface or test/qualification privilege | Consumer include paths and the owning project | [Consumption and access](../../../../Docs/Architecture/Repository_Module_Map.md#public-consumption-and-privileged-access); [module catalog](../../../../Docs/Architecture/Module_Catalog.md) | Boundary checks; owning tests listed by the selected card |

```text
Anchors:
  - GraphicsGadgetLab.sln
ReviewPaths:
  - Projects/**/*.vcxproj
  - Projects/**/*.targets
  - PropertySheets/*.props
  - PropertySheets/*.targets
  - Docs/Architecture/Repository_Module_Map.md
  - AGENTS.md
```

## Frame settlement
Atlas ID: atlas.routes.frame-settlement

- Question: where should a frame-settlement investigation or change begin?
- VerifiedAgainst: `fbb5f36a71fbeacb1350ad68458f04e6c3a3b8cd`
- Refresh triggers: public interfaces, settlement ownership, caller feedback,
  retention rules or owning verification change.

| Task or question | Owning project and source entry | Map/card and governing local contract | Owning verification |
| --- | --- | --- | --- |
| Find the Public rendering interface and its implementation owner | `GGLabRuntime`: [RenderHost.h](../../../../Sources/GGLabRuntime/Public/GGLabRuntime/Graphics/RenderHost.h) → [factory](../../../../Sources/GGLabRuntime/Private/Graphics/RenderHostFactory.cpp) | [Render host card](../../../../Docs/Architecture/Modules/Runtime_Rendering.md#render-host-and-settlement); [dependency rules](../../../../AGENTS.md#ownership-and-dependency-boundaries) | Public/Private project visibility: [boundary validator](../../../../Scripts/ValidateProjectBoundaries.ps1) |
| Explain recorded work that did not complete successfully | `GGLabRuntime`: [outcome derivation](../../../../Sources/GGLabRuntime/Private/Graphics/RenderFrameOutcome.h) → [Renderer::SettleFrame](../../../../Sources/GGLabRuntime/Private/Graphics/Renderer.cpp) | [outcomes and host reaction](../../../../Docs/Architecture/Frame_Execution_Map.md#early-exit-and-outcome-derivation); [Frame settlement](../../../../Docs/Architecture/Frame_Execution_Map.md#frame-settlement) | [Rendering contracts](../../../../Tests/GGLabRuntime/RenderingContractSelfTests.cpp) and [fault lifecycle](../../../../Tests/GGLabAppRuntime/GpuFaultLifecycleSelfTests.cpp) |
| Change deferred CPU publication or handle a late enqueue | `GGLabRuntime`: [RGDeferredCommit.h](../../../../Sources/GGLabRuntime/Public/GGLabRuntime/Graphics/RenderGraph/RGDeferredCommit.h) → [CommitAll](../../../../Sources/GGLabRuntime/Private/Graphics/RenderGraph/RGDeferredCommit.cpp) → Renderer EndFrame; `GGLabAppRuntime`: [TickFrame](../../../../Sources/GGLabAppRuntime/GGLabAppRuntimeFrame.cpp) | [graph card](../../../../Docs/Architecture/Modules/Runtime_Rendering.md#rendergraph-execution-and-deferred-cpu-commits); [Settlement contract failures](../../../../Docs/Architecture/Frame_Execution_Map.md#settlement-contract-failures) | Runtime `rendering-contracts`; AppRuntime `gpu-fault-lifecycle`, including `RunLateCommitScenario` |
| Follow GPU-visible retention and shutdown | `GGLabAppRuntime`: [Shutdown](../../../../Sources/GGLabAppRuntime/GGLabAppRuntimeServices.cpp) → `GGLabRuntime`: [QuiesceRHIForTeardown](../../../../Sources/GGLabRuntime/Private/Graphics/RHI/RHIContext.cpp) → Renderer Finalize | [RHI card](../../../../Docs/Architecture/Modules/Runtime_Rendering.md#rhi-submission-and-resource-retirement); [Teardown ownership](../../../../Docs/Architecture/Frame_Execution_Map.md#teardown-ownership); [checked completion interface](../../../../Sources/GGLabRuntime/Public/GGLabRuntime/Graphics/RHI/RHIDevice.h) | AppRuntime `gpu-fault-lifecycle`; Runtime `rendering-contracts` |
| Review an affected map or a dirty source overlay | Change author: [anchor/impact helper](../../../../Scripts/CheckArchitectureAtlas.ps1), then the reported entry's source; subsystem owner reviews contract changes | [refresh procedure](../../../../Docs/Architecture/Boundary_And_Drift_Guide.md#refresh-one-affected-entry) and [pending refreshes](../../../../Docs/Architecture/Boundary_And_Drift_Guide.md#pending-refreshes) | Helper `-SelfTest`, navigation and source/contract comparison; boundary validation remains separate |

Suite registration and CLI selection are in the [Runtime test main](../../../../Tests/GGLabRuntime/GGLabRuntimeTestsMain.cpp)
and [AppRuntime test main](../../../../Tests/GGLabAppRuntime/GGLabAppRuntimeLifecycleSelfTests.cpp).
GPU capture and visual checks use [gglab-frame-capture](../../gglab-frame-capture/SKILL.md)
when needed.

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

## Render passes
Atlas ID: atlas.routes.render-pass

- Question: where should work on a new or changed render pass begin?
- VerifiedAgainst: `e8d08f13710eb206ea757292293cd327cebdc510`
- Refresh triggers: composition entries, pass ownership, record placement, program
  registration or owning verification change.

| Task or question | Owning project and source entry | Map/card and governing local contract | Owning verification |
| --- | --- | --- | --- |
| Decide who owns a new pass | Decided by what the pass's composition, resources and lifetime require: a Runtime recipe stage in `GGLabRuntime`, or Lab composition in `WinApp` | [Pass ownership](../../../../Docs/Architecture/Runtime_Boundaries_Map.md#pass-ownership); [composition](../../../../Docs/Architecture/Runtime_Boundaries_Map.md#composition) | [Boundary validator](../../../../Scripts/ValidateProjectBoundaries.ps1) for include direction |
| Add a Runtime pass | `GGLabRuntime`: [Forward+ recipe](../../../../Sources/GGLabRuntime/Private/Graphics/RenderPipeline/RenderPipelineForwardPlus.cpp) → a pass under `Private/Graphics/RenderPass/` | [Runtime-owned passes](../../../../Docs/Architecture/Runtime_Boundaries_Map.md#runtime-owned-passes); [render pass card](../../../../Docs/Architecture/Modules/Runtime_Rendering.md#render-pass-authoring); [Imported resource ownership](../../../../Docs/Architecture/Runtime_Boundaries_Map.md#imported-resource-ownership) | Runtime `rendering-contracts`; DX12 and Vulkan GPU runs |
| Add a Lab pass or pipeline | `WinApp`: the Lab session → a Forward+ scene extension or its own pipeline through [LabSessionBase.h](../../../../Sources/GGLabAppRuntime/Lab/LabSessionBase.h) | [Lab-owned composition](../../../../Docs/Architecture/Runtime_Boundaries_Map.md#lab-owned-composition); [render pipeline card](../../../../Docs/Architecture/Modules/Runtime_Rendering.md#render-pipeline-composition) | The Lab's WinApp self-test (for example `napa-voxel`) and `app-content-registration`; `--lab` GPU runs |
| Use a new shader program | `ShaderArtifactRuntime`: [GGLabShaderPrograms.h](../../../../Sources/ShaderArtifactRuntime/Public/ShaderArtifactRuntime/GGLabShaderPrograms.h) → build record → demand | [Program consumption](../../../../Docs/Architecture/Runtime_Boundaries_Map.md#program-consumption); [shader card](../../../../Docs/Architecture/Modules/Runtime_Rendering.md#shader-program-consumption) | Runtime `shader-artifact-runtime`; WinAppTests `app-content-registration`; `GGLabShaderToolchainTests` |

GPU runs use [gglab-frame-capture](../../gglab-frame-capture/SKILL.md).

```text
Anchors:
  - Sources/GGLabRuntime/Private/Graphics/RenderPipeline/RenderPipelineForwardPlus.cpp::RenderPipelineForwardPlus::BuildRenderGraph
  - Sources/GGLabAppRuntime/Lab/LabSessionBase.h::SetRenderPipeline
  - Sources/ShaderArtifactRuntime/Public/ShaderArtifactRuntime/GGLabShaderPrograms.h
ReviewPaths:
  - Docs/Architecture/Runtime_Boundaries_Map.md
  - Docs/Architecture/Modules/Runtime_Rendering.md
  - Tests/GGLabRuntime/RenderingContractSelfTests.cpp
```

## Lab lifetime
Atlas ID: atlas.routes.lab-lifetime

- Question: where should work on a Lab switch, a Lab parameter or the GPU state a Lab
  owns begin?
- VerifiedAgainst: `fbb5f36a71fbeacb1350ad68458f04e6c3a3b8cd`
- Refresh triggers: transition requests, retirement, parameter impacts, session GPU
  ownership, Lab registration or shutdown order change.

| Task or question | Owning project and source entry | Map/card and governing local contract | Owning verification |
| --- | --- | --- | --- |
| Switch or restart a Lab, or explain how long the old session lives | `GGLabAppRuntime`: [LabRuntime.cpp](../../../../Sources/GGLabAppRuntime/Lab/LabRuntime.cpp), from `ProcessPendingCommands` through `CommitPendingSession` to `PollRetiringSessions` | [content transitions](../../../../Docs/Architecture/Frame_Execution_Map.md#content-transitions); [Content retirement](../../../../Docs/Architecture/Frame_Execution_Map.md#content-retirement) | AppRuntime `gpu-fault-lifecycle`, including `RunLabSessionRetirementScenario` |
| Add a Lab parameter or tune a Runtime pass from a Lab | `WinApp`: the Lab session → [LabSessionBase.h](../../../../Sources/GGLabAppRuntime/Lab/LabSessionBase.h) parameter hooks and `ViewRenderProfile` | [content transitions](../../../../Docs/Architecture/Frame_Execution_Map.md#content-transitions); [Lab runtime card](../../../../Docs/Architecture/Modules/Application_Content.md#lab-runtime) | AppRuntime `gpu-fault-lifecycle` (`RunRebuildParameterScenario`); the Lab's WinApp self-test |
| Keep GPU work in a Lab session | `WinApp`: the session; uploads through `GGLabRuntime` [AssetManager.h](../../../../Sources/GGLabRuntime/Public/GGLabRuntime/Graphics/Asset/AssetManager.h) | [Content retirement](../../../../Docs/Architecture/Frame_Execution_Map.md#content-retirement); [Imported resource ownership](../../../../Docs/Architecture/Runtime_Boundaries_Map.md#imported-resource-ownership) for graph imports | WinApp `napa-voxel` as the example; `--lab` GPU runs |
| Add or register a Lab | `WinAppHost`: [DesktopApplicationContent.cpp](../../../../Sources/WinAppHost/Application/Content/DesktopApplicationContent.cpp) → `LabRegistration` in [LabCatalog.h](../../../../Sources/GGLabAppRuntime/Lab/LabCatalog.h) | [Lab runtime card](../../../../Docs/Architecture/Modules/Application_Content.md#lab-runtime); persisted Lab IDs in [AGENTS.md](../../../../AGENTS.md#gpu-shader-and-lab-invariants) | WinAppTests `app-content-registration` |
| Release content at shutdown or after a failed frame | `GGLabAppRuntime`: [Shutdown](../../../../Sources/GGLabAppRuntime/GGLabAppRuntimeServices.cpp) → `DemoManager` reset | [Teardown ownership](../../../../Docs/Architecture/Frame_Execution_Map.md#teardown-ownership); [Content retirement](../../../../Docs/Architecture/Frame_Execution_Map.md#content-retirement) | AppRuntime `gpu-fault-lifecycle` |

```text
Anchors:
  - Sources/GGLabAppRuntime/Lab/LabRuntime.cpp::LabRuntime::CommitPendingSession
  - Sources/GGLabAppRuntime/Lab/LabSessionBase.h::LabSessionBase
  - Sources/WinAppHost/Application/Content/DesktopApplicationContent.cpp::CreateDesktopApplicationContent
ReviewPaths:
  - Docs/Architecture/Frame_Execution_Map.md
  - Docs/Architecture/Modules/Application_Content.md
  - Tests/GGLabAppRuntime/GpuFaultLifecycleSelfTests.cpp
```
