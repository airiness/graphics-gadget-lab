# Frame Execution and Settlement
Atlas ID: atlas.runtime.frame-execution

- Question: what happens to recorded GPU work and dependent CPU state when a frame
  does not complete successfully?
- VerifiedAgainst: `62772447328f54aa8d2814f3489a939173ec2a27`
- Refresh triggers: begin/end outcomes, early-return cleanup, participant order,
  submission feedback, borrowing or GPU-dependent retirement changes.

This map follows the shared AppRuntime frame path and Runtime settlement on DX12 and
Vulkan, then how content is replaced between frames
([content transitions](#content-transitions)); the [overview](README.md) gives the
project layers. Module facts for these paths are in the cards listed in the
[module catalog](Module_Catalog.md).

## One successful frame

Arrows are synchronous calls and returns on the application owner thread, except
the queue submission and the fence note. GPU work outlives command recording and
may finish after EndFrame returns.

```mermaid
sequenceDiagram
    participant A as AppRuntime
    participant R as RenderHost / Renderer
    participant G as RenderGraph
    participant H as RHI context
    participant Q as GPU queues
    A->>R: BeginFrame
    R->>H: BeginFrame
    H-->>R: Ready frame
    R-->>A: Scoped RenderFrame
    A->>R: Temporal planning and BuildFrame
    A->>G: Declare passes and Compile
    A->>R: Render with graph
    R->>G: Execute with active commit list
    G->>H: Record commands
    Note over G,R: Passes may enqueue CPU commits
    A->>R: EndFrame
    R->>H: EndFrame
    H->>Q: Submit commands and fence
    H-->>R: Completed with fence
    R->>R: Settle participants, then CPU commits
    R-->>A: Completed with fence
    A->>A: Submitted feedback and sample advancement
    Note over H,Q: Fence completion gates reuse and readback
```

## Early exit and outcome derivation

Arrows below are **control flow**. The RHI result and the Renderer end/abort path
together determine the settlement.

```mermaid
flowchart TD
    Begin[BeginFrame] --> State{Begin result}
    State -->|Unavailable| Retry[Return without an active Renderer frame]
    State -->|Fatal| Stop[Stop AppRuntime]
    State -->|Ready| Build[Build and validate frame; compile graph]
    Build -->|Early return before submission| Abort[Scoped frame abort]
    Build --> Record[Render and seal commit list]
    Record --> Valid{Recorded and commit list healthy?}
    Valid -->|No| Abort
    Valid -->|Yes| End[Call RHI EndFrame]
    End -->|Completed with fence| Submitted[Submitted]
    End -->|Fatal with or without fence| Failed[Failed]
    End -->|Unknown| Unknown[CompletionUnknown]
    Abort -->|Completion known| Discarded[Discarded]
    Abort -->|Unknown| Unknown
```

After BeginFrame returns Ready, an early return from `TickFrame` destroys the
move-only `RenderFrame`, which aborts the frame through the RenderHost. A frame that
Renderer did not record, for example because its upload-order wait failed, is
aborted inside EndFrame instead.

| Settlement | Meaning and retention |
| --- | --- |
| `Submitted` | EndFrame completed the frame transaction. Its fence orders the frame's work; it does not mean the GPU has finished |
| `Failed` | EndFrame returned Fatal. Work may have executed fully, partly or not at all, so capture fails and temporal history is invalidated. A valid fence still retains resources; an absent fence means no work is outstanding, not that nothing ran |
| `Discarded` | Renderer took its abort path and the recorded commands never execute. Capture requests that did not name this frame are requeued. Graph resources retire on the abort fence, or on the last submitted fence when the abort submitted nothing |
| `CompletionUnknown` | Neither path could verify completion. Renderer is poisoned, no further frame begins, and GPU-visible owners stay retained without ordinary graph retirement |

Three backend details affect these outcomes:

- A submission can succeed and presentation still fail. DX12 can fail Present;
  Vulkan can fail presentation or closing the frame's descriptor arena. These
  frames are Failed with a real fence.
- A backend can also abort inside its own EndFrame after a recording or
  upload-publication failure. The result still comes back through EndFrame, so the
  frame settles as Failed although its commands never ran: the outcome follows the
  Renderer path, not GPU execution.
- Abort does not execute the recorded commands, but the native abort can still
  submit work, for example to signal retirement or release an acquired swapchain
  image. Vulkan's abort may need a fence or a checked idle wait, so an abort can
  also end in CompletionUnknown.

AppRuntime decides separately whether to keep running:

| `TickFrame` path | Settlement | AppRuntime |
| --- | --- | --- |
| Early return after sequence rejection, skipped validation or resource-export deferral | Discarded | Continues with the next tick |
| Early return after validation or graph compilation failure | Discarded | Stops |
| EndFrame returns Completed | Submitted | Sends submitted feedback |
| EndFrame returns Fatal | Failed, CompletionUnknown, Discarded (unrecorded frame) or Submitted ([late enqueue](#settlement-contract-failures)) | Stops without feedback |

When refreshing this map, recheck three easily missed paths first: the late-enqueue
exception, both native abort paths and unverified teardown.

```text
Anchors:
  - Sources/GGLabAppRuntime/GGLabAppRuntimeFrame.cpp::TickFrame
  - Sources/GGLabRuntime/Private/Graphics/Renderer.cpp::Renderer::SettleFrame
  - Sources/GGLabRuntime/Private/Graphics/RenderFrameOutcome.h::ResolveRenderFrameOutcome
ReviewPaths:
  - Sources/GGLabRuntime/Private/Graphics/RHI/DX12/DX12Context.cpp
  - Sources/GGLabRuntime/Private/Graphics/RHI/Vulkan/VulkanContext.cpp
  - Sources/GGLabAppRuntime/GGLabAppRuntimeServices.cpp
```

## Content transitions
Atlas ID: atlas.application.content-transitions

- Question: what happens to a Demo or Lab session, and to what it owns, when content
  is switched, rebuilt or shut down?
- VerifiedAgainst: `96ba1316b36a31bb1a983b0cf5719b55cc4427fa`
- Refresh triggers: transition requests or safe points, preparation, activation,
  retirement, parameter impacts or shutdown order change.

AppRuntime hosts content at two levels: `DemoManager` switches Demos, and the
`LabRuntime` inside the Lab host Demo switches Lab sessions. Both replace content
the same way; the [Content retirement](#content-retirement) rule says how long each
state keeps what it owns. Arrows are state changes.

```mermaid
stateDiagram-v2
    [*] --> Pending: request consumed at the safe point
    Pending --> Released: cancelled, superseded or failed
    Pending --> Active: preparation ready
    Active --> Retiring: replaced
    Retiring --> Released: retire fence completed, or shutdown
    Active --> Released: shutdown
    Released --> [*]
```

| Step | Demos | Lab sessions |
| --- | --- | --- |
| Request | `DemoManager::RequestActiveDemo` | `ILabControl`, queued in `LabCommandQueue` |
| Safe point | `BeginTransitionTick` and `CompleteTransitionTick` in `TickFrame`, before the Demo's `Update` | `LabRuntime::ProcessPendingCommands`, called from `DemoLabHost::Update` before `BeginFrame` |
| Activation | Parameter and sequence bindings are released, then the old Demo exits and retires | The old session exits and retires; the temporal session serial advances |
| Release | `PollRetiringDemos` | `PollRetiringSessions` |

The active content keeps rendering while its replacement prepares. A retiring entry
holds the fence of the last frame that sent feedback, and only frames whose EndFrame
returned Completed send it. `DemoManager::GetActiveContentIdentity` combines the two
serials; AppRuntime rebinds parameter control and frame sequences to it.

Inside a Lab session:

| Change | How it applies |
| --- | --- |
| Switch | A new pending session of the target Lab |
| Restart or run configuration | A new pending session of the same Lab, restoring the current parameter values |
| Parameter with `RebuildScene`, `RecreatePipeline` or `RestartSession` impact | A new pending session; never in place |
| `Immediate` parameter | In place, through `ApplyImmediateParameters` and the session's `ViewRenderProfile` |

A Lab tunes a Runtime pass through its `ViewRenderProfile`, which resolves into
per-view settings each frame, as `GTAOLabSession` does for GTAO. Strict parameter
batches and their receipts are coordinated by
[SessionParameterControl](../../Sources/GGLabAppRuntime/Parameters/SessionParameterControl.h).
Napa shows session-owned GPU work: `OnFrameSubmitted` records the last-use graphics
fence, and `Update` polls completion to retire replaced mesh sets. Preparation
uploads go through the asset upload scheduler.

```text
Anchors:
  - Sources/GGLabAppRuntime/Demo/DemoManager.cpp::DemoManager::CommitPendingDemo
  - Sources/GGLabAppRuntime/Lab/LabRuntime.cpp::LabRuntime::ProcessPendingCommands
  - Sources/GGLabAppRuntime/Lab/LabRuntime.cpp::LabRuntime::CommitPendingSession
  - Sources/WinApp/Application/Demo/DemoLabHost.cpp::DemoLabHost::Update
ReviewPaths:
  - Sources/GGLabAppRuntime/Lab/LabRuntimeParameters.cpp
  - Sources/GGLabAppRuntime/GGLabAppRuntimeFrame.cpp
  - Sources/WinApp/Application/Lab/Sessions/NapaVoxelLabSession.cpp
```

## Contracts
Atlas ID: atlas.runtime.frame-contracts

The rules below cover cross-participant coordination that no single interface
owns. They detail the GPU and lifetime rules in
[AGENTS.md](../../AGENTS.md#gpu-shader-and-lab-invariants); keep both consistent
when either changes. Per-interface rules stay in
[RGDeferredCommit.h](../../Sources/GGLabRuntime/Public/GGLabRuntime/Graphics/RenderGraph/RGDeferredCommit.h),
[RHIContext.h](../../Sources/GGLabRuntime/Public/GGLabRuntime/Graphics/RHI/RHIContext.h),
[RHIDevice.h](../../Sources/GGLabRuntime/Public/GGLabRuntime/Graphics/RHI/RHIDevice.h)
and [RHIFence.h](../../Sources/GGLabRuntime/Public/GGLabRuntime/Graphics/RHI/RHIFence.h).

### Frame settlement

Each frame derives one private outcome from the Renderer path and the RHI result
(`ResolveRenderFrameOutcome`) and settles its Runtime participants once, in
`Renderer::SettleFrame`, in these orders:

| Outcome | Required Runtime participant order |
| --- | --- |
| Submitted / Failed | Runtime capture → live/bake atmosphere → temporal transaction/history → last submitted fence and IBL scheduler → graph retirement |
| Discarded | Live/bake atmosphere → IBL scheduler → last submitted fence → temporal transaction → graph retirement → Runtime capture |
| CompletionUnknown | Poison → fail/retain Runtime capture → retain live/bake atmosphere → abort IBL scheduling → abort/invalidate temporal state; no ordinary graph retirement |

These are the implementation's baseline orders, also stated in comments inside
`Renderer::SettleFrame`; change the table and those comments together. One
placement has a recorded reason: on the abort path, capture releases its
unsubmitted taps last, after the RHI abort has recorded the frame's resource uses.
Review ownership and fence dependencies before changing or unifying participant order.

Plain CPU deferred commits run only for Submitted: once each, in enqueue order,
after every participant. Every other outcome discards them, and so does
frame-lifetime cleanup. This keeps recorded work from publishing CPU state before
its submission is accepted, while GPU-dependent owners are still retained when the
transaction fails. Passes must not reproduce this policy.

### Settlement contract failures

Renderer seals the frame's commit list whenever `Render` exits. An enqueue that
arrives after that, for example through an enqueue capability kept past `Render`,
is rejected and marks the list failed. The outcome depends on when it arrives:

| Late enqueue arrives | Runtime participants | CPU commits | EndFrame returns |
| --- | --- | --- | --- |
| Before RHI submission | Abort path: Discarded, or CompletionUnknown | Discarded | The abort's result |
| After the RHI accepted the submission, before the commits start | Settled as Submitted | Discarded | Fatal with the real submitted fence |
| While the commits run | Settled as Submitted | The original batch runs once | Fatal with the real submitted fence |

After an accepted submission nothing is rolled back or settled again. A failure
seen before the commits start cancels them; once they start, the original batch
completes rather than publishing only part of it. In both post-submission rows the
layers disagree: Runtime participants settled as Submitted, yet EndFrame returns
Fatal, so AppRuntime stops without sending submitted feedback. An enqueue after
EndFrame returns is outside this contract, because `RGExecuteContext` must not be
retained beyond execution.

### Teardown ownership

Shut down in this order:

1. Stop new producer work and drain or cancel the application work that depends on
   Runtime services.
2. Establish verified idle or natively confirmed device loss
   (`QuiesceRHIForTeardown`) before destroying GPU-visible owners, or tooling,
   content and assets that borrow GPU services.
3. Release those consumers while the Runtime services they borrow are still alive.
4. Finalize and release the RenderHost.

Unknown completion and timed-out waits do not count as device loss. When teardown
ends in `RHITeardownState::Unverified` on a live device, GPU-visible memory is
leaked rather than destroyed: AppRuntime shutdown exits the process without
destructing it, and `Renderer::Finalize` abandons its retained owners. After
confirmed loss, owners may be destroyed without fence waits, but GPU-written
contents stay unusable.

```text
Anchors:
  - Sources/GGLabRuntime/Private/Graphics/Renderer.cpp::Renderer::SettleFrame
  - Sources/GGLabRuntime/Private/Graphics/Renderer.cpp::Renderer::EndFrame
  - Sources/GGLabAppRuntime/GGLabAppRuntimeFrame.cpp::TickFrame
  - Sources/GGLabAppRuntime/GGLabAppRuntimeServices.cpp::Shutdown
ReviewPaths:
  - Sources/GGLabRuntime/Private/Graphics/RenderGraph/RGDeferredCommit.cpp
  - Sources/GGLabRuntime/Private/Graphics/RHI/RHIContext.cpp
  - Tests/GGLabAppRuntime/GpuFaultLifecycleSelfTests.cpp
```

### Content retirement
Atlas ID: atlas.application.content-retirement

- VerifiedAgainst: `96ba1316b36a31bb1a983b0cf5719b55cc4427fa`
- Refresh triggers: retirement fences, pending-content release, session-owned GPU
  work or failure handling change.

Content is a Demo or a Lab session together with what it owns: its World, cameras,
pipeline, asset interests and fence-dependent work.

- Replaced content stays alive until the GPU has finished the last frame that drew
  it and whose EndFrame returned Completed. Content that no such frame drew may be
  released at the next poll.
- Pending content is released as soon as its preparation is cancelled, superseded or
  fails. GPU work it starts belongs to a service that retains it until completion,
  such as the asset upload scheduler.
- Active content advances last-use frame fences only from submitted-frame feedback.
  Completion polling, including from `Update`, may release resources after their
  recorded fences complete.
- After a Failed or CompletionUnknown frame, AppRuntime stops ticking, and content is
  released only after [Teardown ownership](#teardown-ownership) establishes
  quiescence.

```text
Anchors:
  - Sources/GGLabAppRuntime/Lab/LabRuntime.cpp::LabRuntime::PollRetiringSessions
  - Sources/GGLabAppRuntime/Demo/DemoManager.cpp::DemoManager::PollRetiringDemos
  - Sources/GGLabAppRuntime/Lab/LabSessionBase.h::BeginPrepare
ReviewPaths:
  - Sources/GGLabAppRuntime/Demo/DemoBase.h
  - Sources/WinApp/Application/Lab/Sessions/NapaVoxelLabSession.cpp
  - Sources/WinApp/Application/Lab/NapaVoxel/NapaVoxelRenderState.cpp
  - Tests/GGLabAppRuntime/GpuFaultLifecycleSelfTests.cpp
```
