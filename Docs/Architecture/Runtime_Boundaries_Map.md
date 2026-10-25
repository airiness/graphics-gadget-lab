# Runtime Boundaries and Render Pass Composition
Atlas ID: atlas.runtime.composition-boundaries

- Question: where does a new render pass belong, and through which seams does it
  reach the frame?
- VerifiedAgainst: `e8d08f13710eb206ea757292293cd327cebdc510`
- Refresh triggers: pipeline composition entries, the temporal boundary, Public or
  Private record placement, import and retention rules, or program demand
  registration change.

This map covers how passes are composed into a frame's RenderGraph. What happens
after the graph is built is in the [frame execution map](Frame_Execution_Map.md);
module facts are in the cards listed in the [module catalog](Module_Catalog.md).

## Composition

Arrows are calls during graph construction; labels name the Public seam.

```mermaid
flowchart TD
    App[AppRuntime TickFrame] -->|RenderPipelineBase| Recipe[Forward+ recipe: Runtime Private]
    App -->|RenderPipelineBase| LabPipeline[Lab-owned pipeline: WinApp]
    Recipe --> RuntimePasses[Runtime passes: Runtime Private]
    Recipe -->|scene extension, post-TAA| Extension[Lab scene extension: WinApp]
    RuntimePasses --> Graph[RenderGraph builder: Public]
    Extension --> Graph
    LabPipeline --> Graph
```

AppRuntime calls the content's pipeline through `RenderPipelineBase`: it validates
the frame first and builds the graph only for a Ready frame. The pipeline is either
the Runtime Forward+ recipe, created with `CreateRenderPipelineForwardPlus`, or one
a Lab installs with `LabSessionBase::SetRenderPipeline`. Which side owns a new pass
is decided by [Pass ownership](#pass-ownership).

## Runtime-owned passes

A Runtime pass is a stage of a Runtime recipe:

1. Implement it under `Sources/GGLabRuntime/Private/Graphics/RenderPass/`, usually
   deriving `RenderPassBase`.
2. Add it at its position in the recipe's `BuildRenderGraph`. Anything that can make
   the frame unrenderable is classified in `ValidateRenderFrame` first.
3. Declare every graph access through the pass builder and exchange records through
   the blackboard. Records that only Runtime passes use stay Private.
4. Keep prepared pipelines in the pass. History that must commit with the frame goes
   through a settlement participant, as `RenderPassGTAO` does with
   `TemporalFrameTransaction`.
5. Publish frame-dependent CPU state with `DeferCommit`.
6. Use programs as described in [Program consumption](#program-consumption).

`RenderPassForwardPlusCull` shows transient records and a fenced readback;
`RenderPassGTAO` shows temporal history.

## Lab-owned composition

A Lab composes graph work through Public seams only. Two routes exist today:

| Route | Position | Example |
| --- | --- | --- |
| Forward+ scene extension, passed as `RenderPipelineForwardPlusCreateInfo::m_SceneExtension` | The current post-TAA entry: called once after the temporal boundary and before transparent shading, writing display color and depth | `NapaVoxelLabSession` → `NapaVoxelRenderExtension` → `NapaVoxelRenderPass` |
| Lab-owned pipeline, a `RenderPipelineBase` installed with `SetRenderPipeline` | The Lab owns the whole graph topology; no Runtime recipe stage runs | `RenderGraphComputeLabSession` |

`RenderPassBase` is optional on both routes; the compute Lab declares its passes
inline. Neither route can insert a pass between Runtime recipe stages, and a scene
extension cannot take part in temporal integration yet. The overlay extension that
runs after post-processing belongs to tooling and is not a Lab route. Lab work
follows the same declaration, retention and commit rules as Runtime passes and
registers its programs with the Lab.

## Program consumption

A pass that uses a new shader program:

1. References it as a `shader_programs` entry in `GGLabShaderPrograms.h`.
2. Adds its build record in `Sources/Tools/ShaderCompiler/GGLabRuntimeShaderBuild.cpp`;
   a new shader source also needs `Scripts/SyncShadersToVS.ps1`.
3. Registers demand: production programs in `GetRendererInitialShaderProgramDemand`,
   Lab programs in its `LabRegistration::m_ShaderPrograms`.
4. Loads it through `RenderServices::m_ShaderPrograms` and resolves pipelines through
   `RenderServices::m_PipelineResolver`.

Shader production and artifact publication are outside this map.

```text
Anchors:
  - Sources/GGLabRuntime/Public/GGLabRuntime/Graphics/RenderPipeline/RenderPipelineBase.h::RenderPipelineBase
  - Sources/GGLabRuntime/Private/Graphics/RenderPipeline/RenderPipelineForwardPlus.cpp::RenderPipelineForwardPlus::BuildRenderGraph
  - Sources/GGLabRuntime/Public/GGLabRuntime/Graphics/RenderPipeline/RenderPipelineSceneExtensionBase.h::AddOpaqueScenePasses
  - Sources/GGLabAppRuntime/Lab/LabSessionBase.h::SetRenderPipeline
  - Sources/ShaderArtifactRuntime/Public/ShaderArtifactRuntime/GGLabShaderPrograms.h
ReviewPaths:
  - Sources/GGLabRuntime/Private/Graphics/Shader/ShaderProgramCatalog.cpp
  - Sources/Tools/ShaderCompiler/GGLabRuntimeShaderBuild.cpp
  - Sources/GGLabAppRuntime/Lab/LabCatalog.h
  - Sources/GGLabRuntime/Public/GGLabRuntime/Graphics/Pipeline/TemporalAA.h
```

## Contracts
Atlas ID: atlas.runtime.composition-contracts

The two rules below cover pass ownership and the lifetime of resources shared with a
graph. They detail the [ownership](../../AGENTS.md#ownership-and-dependency-boundaries)
and [RenderGraph](../../AGENTS.md#gpu-shader-and-lab-invariants) rules in AGENTS.md;
keep both consistent when either changes. Per-interface rules stay in
[RenderGraph.h](../../Sources/GGLabRuntime/Public/GGLabRuntime/Graphics/RenderGraph/RenderGraph.h)
and the pipeline headers.

### Pass ownership

Decide a pass's owner from what its composition, resources and lifetime require, not
from whether it could be reused:

- It is Runtime-owned when the production recipe requires it or must place it, for
  example before the temporal boundary or between Runtime passes; when it reads or
  publishes a Private record that Runtime passes exchange; or when it requires
  participation in a Runtime-owned settlement transaction, such as temporal history.
- Otherwise it can be Lab-owned and composed through a Public seam. Reuse across Labs
  does not by itself make a pass Runtime-owned.
- WinApp never depends on Runtime Private. A Lab that needs a Private record or a
  position the recipe does not offer asks for a Runtime change: a Runtime pass, a new
  Public record or a reviewed new seam.

### Imported resource ownership

Importing a resource into a RenderGraph does not transfer its ownership, and the code
that imports it need not be its owner. The graph retires only the transients it
creates. The resource's owner keeps it, and any contents the frame reads, valid until
all GPU work that may access it has safely finished: the fence of the last Submitted
frame that used it, and the retention fence of a Failed frame. After
CompletionUnknown the owner follows [Teardown ownership](Frame_Execution_Map.md#teardown-ownership).
The importer declares the initial access and content validity, and the exporting pass
declares the final access.

```text
Anchors:
  - Sources/GGLabRuntime/Public/GGLabRuntime/Graphics/RenderGraph/RenderGraph.h::Retire
  - Sources/GGLabRuntime/Private/Graphics/RenderPipeline/RenderPipelineForwardPlus.cpp::ResolveTemporalFramePlan
ReviewPaths:
  - Sources/GGLabRuntime/Private/Graphics/RenderGraph/RenderGraph.cpp
  - Sources/WinApp/Application/Lab/NapaVoxel/NapaVoxelRenderPass.cpp
  - Sources/WinApp/Application/Lab/Sessions/NapaVoxelLabSession.cpp
```
