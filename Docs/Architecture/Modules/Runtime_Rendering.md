# Runtime Rendering Cards

- Question: which Runtime module owns each rendering responsibility, and where are
  its interfaces?
- VerifiedAgainst: `e8d08f13710eb206ea757292293cd327cebdc510`
- Refresh triggers: a card's source owner, Public interface, lifetime, linked
  contract or owning tests change.

The [module catalog](../Module_Catalog.md) lists all cards.

## Render host and settlement
Atlas ID: atlas.runtime.render-host

- **Source owner:** `GGLabRuntime`; Public RenderHost interface, Private Renderer.
- **Responsibility:** the active frame, Runtime participants, the commit list and
  GPU services.
- **Public interface:** [RenderHost.h](../../../Sources/GGLabRuntime/Public/GGLabRuntime/Graphics/RenderHost.h),
  including `CreateRenderHost` and the move-only `RenderFrame` abort handle;
  [RenderServicesView.h](../../../Sources/GGLabRuntime/Public/GGLabRuntime/Graphics/RenderServicesView.h)
  provides retained service access.
- **Implementation:** [Renderer](../../../Sources/GGLabRuntime/Private/Graphics/Renderer.cpp)
  and [outcome derivation](../../../Sources/GGLabRuntime/Private/Graphics/RenderFrameOutcome.h).
- **Lifetime:** AppRuntime owns the host. Renderer owns the RHI context and pools,
  borrows the active graph and clears its active-frame state after settlement.
  Service views revoke before teardown callbacks; contained borrows end before Finalize.
- **Boundary:** [frame settlement](../Frame_Execution_Map.md#frame-settlement) keeps
  the four outcomes and their retention fences apart.
- **Verification:** the production Renderer in `GpuFaultLifecycleSelfTests.cpp`;
  outcome and commit-list contracts in `RenderingContractSelfTests.cpp`.

```text
Anchors:
  - Sources/GGLabRuntime/Private/Graphics/Renderer.cpp::Renderer::EndFrame
  - Sources/GGLabRuntime/Public/GGLabRuntime/Graphics/RenderHost.h::RenderFrame::Reset
ReviewPaths:
  - Sources/GGLabRuntime/Private/Graphics/RenderHostFactory.cpp
  - Sources/GGLabRuntime/Public/GGLabRuntime/Graphics/RenderServicesView.h
  - Sources/GGLabRuntime/Private/Graphics/RenderFrameOutcome.h
```

## Render pipeline composition
Atlas ID: atlas.runtime.render-pipeline

- **Source owner:** `GGLabRuntime`; Public pipeline seams, Private Forward+ recipe.
- **Responsibility:** classify each frame before graph construction, then build its
  graph; offer the scene and overlay extension seams.
- **Public interface:** [RenderPipelineBase.h](../../../Sources/GGLabRuntime/Public/GGLabRuntime/Graphics/RenderPipeline/RenderPipelineBase.h),
  [RenderPipelineForwardPlus.h](../../../Sources/GGLabRuntime/Public/GGLabRuntime/Graphics/RenderPipeline/RenderPipelineForwardPlus.h)
  (`CreateRenderPipelineForwardPlus` and its create info),
  [RenderPipelineSceneExtensionBase.h](../../../Sources/GGLabRuntime/Public/GGLabRuntime/Graphics/RenderPipeline/RenderPipelineSceneExtensionBase.h)
  and [RenderPipelineBlackboard.h](../../../Sources/GGLabRuntime/Public/GGLabRuntime/Graphics/RenderPipeline/RenderPipelineBlackboard.h).
- **Implementation:** [Forward+ recipe](../../../Sources/GGLabRuntime/Private/Graphics/RenderPipeline/RenderPipelineForwardPlus.cpp)
  and its [factory](../../../Sources/GGLabRuntime/Private/Graphics/RenderPipeline/RenderPipelineForwardPlusFactory.cpp).
- **Lifetime:** a Lab session owns its pipeline through `LabSessionBase`. The Forward+
  recipe owns its Runtime passes and the scene extension it was created with.
- **Boundary:** [pass ownership](../Runtime_Boundaries_Map.md#pass-ownership); the
  scene extension is the current post-TAA entry, and `TemporalAA.h` records why it
  cannot join temporal integration yet.
- **Verification:** `GGLabRuntimeTests` suite `rendering-contracts`, including the
  scene and overlay extension contract tests and the Forward+ contract tests.

```text
Anchors:
  - Sources/GGLabRuntime/Private/Graphics/RenderPipeline/RenderPipelineForwardPlus.cpp::RenderPipelineForwardPlus::BuildRenderGraph
  - Sources/GGLabRuntime/Public/GGLabRuntime/Graphics/RenderPipeline/RenderPipelineBase.h::ValidateRenderFrame
ReviewPaths:
  - Sources/GGLabRuntime/Private/Graphics/RenderPipeline/RenderPipelineForwardPlusFactory.cpp
  - Sources/GGLabAppRuntime/Lab/LabSessionBase.h
```

## Render pass authoring
Atlas ID: atlas.runtime.render-pass-authoring

- **Source owner:** `GGLabRuntime` owns the Public authoring seams and the Runtime
  passes under `Private/Graphics/RenderPass/`; Lab passes belong to WinApp.
- **Responsibility:** add declared graph work for one stage, keep prepared pipelines
  and any per-pass state.
- **Public interface:** [RenderPassBase.h](../../../Sources/GGLabRuntime/Public/GGLabRuntime/Graphics/RenderPass/RenderPassBase.h)
  (optional base), the pass builder in
  [RenderGraph.h](../../../Sources/GGLabRuntime/Public/GGLabRuntime/Graphics/RenderGraph/RenderGraph.h)
  and Public records such as
  [SceneDepthGraphResources.h](../../../Sources/GGLabRuntime/Public/GGLabRuntime/Graphics/RenderPass/SceneDepthGraphResources.h).
- **Implementation:** Runtime examples [RenderPassForwardPlusCull](../../../Sources/GGLabRuntime/Private/Graphics/RenderPass/RenderPassForwardPlusCull.cpp)
  and [RenderPassGTAO](../../../Sources/GGLabRuntime/Private/Graphics/RenderPass/RenderPassGTAO.cpp);
  the Lab example [NapaVoxelRenderPass](../../../Sources/WinAppHost/Application/Lab/NapaVoxel/NapaVoxelRenderPass.cpp).
- **Lifetime:** a pass lives as long as its pipeline or extension; graph transients
  live for one frame; persistent resources belong to an owner outside the graph.
- **Boundary:** [pass ownership](../Runtime_Boundaries_Map.md#pass-ownership) and
  [imported resource ownership](../Runtime_Boundaries_Map.md#imported-resource-ownership).
  Records that only Runtime passes exchange, such as `RGForwardPlusResources`, stay
  Private.
- **Verification:** `GGLabRuntimeTests` suite `rendering-contracts`, including the
  render graph access, Forward+ and GTAO history tests and the Napa voxel graph
  fixture; `WinAppTests --suite napa-voxel`; GPU runs with the
  [frame-capture skill](../../../.agents/skills/gglab-frame-capture/SKILL.md).

```text
Anchors:
  - Sources/GGLabRuntime/Public/GGLabRuntime/Graphics/RenderPass/RenderPassBase.h::RenderPassBase
  - Sources/GGLabRuntime/Private/Graphics/RenderPass/RenderPassForwardPlusCull.cpp::RenderPassForwardPlusCull::AddPass
  - Sources/WinAppHost/Application/Lab/NapaVoxel/NapaVoxelRenderPass.cpp::NapaVoxelRenderPass::AddPass
ReviewPaths:
  - Sources/GGLabRuntime/Private/Graphics/RenderPass/RenderPassGTAO.cpp
  - Tests/GGLabRuntime/RenderingContractSelfTests.cpp
```

## RenderGraph execution and deferred CPU commits
Atlas ID: atlas.runtime.graph-execution

- **Source owner:** `GGLabRuntime`, RenderGraph subtree.
- **Responsibility:** compile declared dependencies and execute the surviving passes.
  Released graph handles go to frame retirement, not immediate physical reuse.
  Imported resources take part in ordering and barriers from their declared initial
  access, and an export sets their final access.
- **Public interface:** [RenderGraph.h](../../../Sources/GGLabRuntime/Public/GGLabRuntime/Graphics/RenderGraph/RenderGraph.h)
  and [RGDeferredCommit.h](../../../Sources/GGLabRuntime/Public/GGLabRuntime/Graphics/RenderGraph/RGDeferredCommit.h).
- **Implementation:** [RenderGraph](../../../Sources/GGLabRuntime/Private/Graphics/RenderGraph/RenderGraph.cpp),
  [RGExecutor](../../../Sources/GGLabRuntime/Private/Graphics/RenderGraph/RGExecutor.cpp)
  and the [commit list](../../../Sources/GGLabRuntime/Private/Graphics/RenderGraph/RGDeferredCommit.cpp).
- **Lifetime:** execution borrows the command contexts, the transient pool and the
  caller-owned commit list. Culled passes enqueue nothing. Renderer settles the list
  it supplies for the active frame.
- **Boundary:** graph ownership rules are in
  [AGENTS.md](../../../AGENTS.md#gpu-shader-and-lab-invariants);
  [frame settlement](../Frame_Execution_Map.md#frame-settlement) decides when CPU
  state may publish, and
  [imported resource ownership](../Runtime_Boundaries_Map.md#imported-resource-ownership)
  keeps imports with their owner.
- **Verification:** `GGLabRuntimeTests` suite `rendering-contracts`, plus the
  production fault harness.

```text
Anchors:
  - Sources/GGLabRuntime/Private/Graphics/RenderGraph/RenderGraph.cpp::RenderGraph::Retire
  - Sources/GGLabRuntime/Private/Graphics/RenderGraph/RGDeferredCommit.cpp::RGDeferredCommitList::CommitAll
ReviewPaths:
  - Sources/GGLabRuntime/Private/Graphics/RenderGraph/RGExecutor.cpp
  - Sources/GGLabRuntime/Public/GGLabRuntime/Graphics/RenderGraph/RenderGraph.h
  - Sources/GGLabRuntime/Public/GGLabRuntime/Graphics/RenderGraph/RGDeferredCommit.h
```

## Shader program consumption
Atlas ID: atlas.runtime.shader-consumption

- **Source owner:** `ShaderArtifactRuntime` owns program references, `GGLabRuntime`
  owns loading and production demand, and the `ShaderCompiler` tool owns build
  records.
- **Responsibility:** name a program, build it, declare when it must be ready and
  load it for a pass.
- **Public interface:** [GGLabShaderPrograms.h](../../../Sources/ShaderArtifactRuntime/Public/ShaderArtifactRuntime/GGLabShaderPrograms.h);
  `m_ShaderPrograms` and `m_PipelineResolver` in
  [RenderServices.h](../../../Sources/GGLabRuntime/Public/GGLabRuntime/Graphics/RenderServices.h).
- **Implementation:** [production demand](../../../Sources/GGLabRuntime/Private/Graphics/Shader/ShaderProgramCatalog.cpp),
  [build records](../../../Sources/Tools/ShaderCompiler/GGLabRuntimeShaderBuild.cpp)
  and Lab demand in `LabRegistration::m_ShaderPrograms`
  ([LabCatalog.h](../../../Sources/GGLabAppRuntime/Lab/LabCatalog.h)).
- **Boundary:** shader production, artifact publication and activation are outside
  this card.
- **Verification:** `GGLabRuntimeTests` suite `shader-artifact-runtime`; WinApp
  `WinAppTests --suite app-content-registration` for Lab demand; `GGLabShaderToolchainTests`
  checks the build-record catalog count when records change.

```text
Anchors:
  - Sources/ShaderArtifactRuntime/Public/ShaderArtifactRuntime/GGLabShaderPrograms.h
  - Sources/GGLabRuntime/Private/Graphics/Shader/ShaderProgramCatalog.cpp::GetRendererInitialShaderProgramDemand
ReviewPaths:
  - Sources/Tools/ShaderCompiler/GGLabRuntimeShaderBuild.cpp
  - Sources/GGLabAppRuntime/Lab/LabCatalog.h
  - Tests/ShaderToolchain/ShaderCompilerCliContractSelfTests.cpp
```

## RHI submission and resource retirement
Atlas ID: atlas.runtime.rhi-submission

- **Source owner:** `GGLabRuntime`; neutral RHI plus Private DX12 and Vulkan
  backends. Vulkan sources compile only when `GGLAB_ENABLE_VULKAN != 0`.
- **Responsibility:** native submit, present and abort; upload retention; checked
  completion.
- **Public interface:** [RHIContext.h](../../../Sources/GGLabRuntime/Public/GGLabRuntime/Graphics/RHI/RHIContext.h),
  [RHIDevice.h](../../../Sources/GGLabRuntime/Public/GGLabRuntime/Graphics/RHI/RHIDevice.h)
  and [RHIFence.h](../../../Sources/GGLabRuntime/Public/GGLabRuntime/Graphics/RHI/RHIFence.h).
- **Implementation:** [submission resolution](../../../Sources/GGLabRuntime/Private/Graphics/RHI/RHIFrameSubmission.cpp),
  [DX12](../../../Sources/GGLabRuntime/Private/Graphics/RHI/DX12/DX12Context.cpp),
  [Vulkan](../../../Sources/GGLabRuntime/Private/Graphics/RHI/Vulkan/VulkanContext.cpp)
  and the [transient pool](../../../Sources/GGLabRuntime/Private/Graphics/Resource/TransientResourcePool.cpp).
- **Lifetime:** Renderer owns the context and pool. Backend frame and upload owners
  keep accepted work until its completion permits release.
- **Boundary:** [teardown ownership](../Frame_Execution_Map.md#teardown-ownership)
  coordinates consumers; native fence and device-loss policy stay in the backends.
- **Verification:** Runtime submission contracts and AppRuntime's simulated fault
  harness.

```text
Anchors:
  - Sources/GGLabRuntime/Private/Graphics/RHI/RHIFrameSubmission.cpp::ResolveFrameSubmission
  - Sources/GGLabRuntime/Private/Graphics/RHI/RHIContext.cpp::QuiesceRHIForTeardown
ReviewPaths:
  - Sources/GGLabRuntime/Private/Graphics/RHI/DX12/DX12Context.cpp
  - Sources/GGLabRuntime/Private/Graphics/RHI/Vulkan/VulkanContext.cpp
  - Sources/GGLabRuntime/Private/Graphics/Resource/TransientResourcePool.cpp
```
