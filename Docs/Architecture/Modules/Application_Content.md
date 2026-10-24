# Application Content Cards

- Question: which application module owns frame orchestration and content hosting,
  and where are its interfaces?
- VerifiedAgainst: `96ba1316b36a31bb1a983b0cf5719b55cc4427fa`
- Refresh triggers: a card's source owner, Public interface, lifetime, linked
  contract or owning tests change.

The [module catalog](../Module_Catalog.md) lists all cards.

## Application orchestration
Atlas ID: atlas.application.frame-orchestration

- **Source owner:** `GGLabAppRuntime`, under `Sources/GGLabAppRuntime`.
- **Responsibility:** begin, build, validate, execute and end each frame, then send
  host-level feedback. WinApp calls in through `GGLabAppRuntime::Tick`.
- **Public interface:** [GGLabAppRuntime.h](../../../Sources/GGLabAppRuntime/GGLabAppRuntime.h).
- **Implementation:** [TickFrame](../../../Sources/GGLabAppRuntime/GGLabAppRuntimeFrame.cpp)
  and [InitializeServices / Shutdown](../../../Sources/GGLabAppRuntime/GGLabAppRuntimeServices.cpp).
- **Lifetime:** owns the RenderHost and shared services; the RenderGraph lives for one
  `TickFrame`. Composition and service pointers, and the ready frame's abort
  cleanup, borrow the host.
- **Boundary:** [success feedback](../Frame_Execution_Map.md#settlement-contract-failures)
  waits for Runtime settlement; [teardown](../Frame_Execution_Map.md#teardown-ownership)
  releases consumers while their borrowed services are alive.
- **Verification:** `GGLabAppRuntimeTests` suite `gpu-fault-lifecycle`, including
  late-enqueue feedback suppression and safe shutdown. The test project declares a
  Private RHI include path for its fault harness.

```text
Anchors:
  - Sources/GGLabAppRuntime/GGLabAppRuntimeFrame.cpp::TickFrame
  - Sources/GGLabAppRuntime/GGLabAppRuntimeServices.cpp::Shutdown
ReviewPaths:
  - Sources/WinApp/Application/Application.cpp
  - Projects/GGLabAppRuntime/GGLabAppRuntime.vcxproj
```

## Content hosting
Atlas ID: atlas.application.content-hosting

- **Source owner:** `GGLabAppRuntime`, under `Sources/GGLabAppRuntime/Demo`.
  WinApp supplies the Demo factories through its content registration.
- **Responsibility:** switch Demos at a frame-safe point and keep each replaced Demo
  until its retire fence completes.
- **Interface:** [DemoBase.h](../../../Sources/GGLabAppRuntime/Demo/DemoBase.h)
  (preparation, enter/exit, update and frame feedback) and
  [DemoManager.h](../../../Sources/GGLabAppRuntime/Demo/DemoManager.h).
- **Implementation:** [DemoManager.cpp](../../../Sources/GGLabAppRuntime/Demo/DemoManager.cpp).
- **Lifetime:** AppRuntime owns `DemoManager`, which owns the active, pending and
  retiring Demos.
- **Boundary:** [content transitions](../Frame_Execution_Map.md#content-transitions);
  [Content retirement](../Frame_Execution_Map.md#content-retirement).
- **Verification:** `GGLabAppRuntimeTests` suite `gpu-fault-lifecycle`, including
  Demo retirement behind a held fence and binding release before exit.

```text
Anchors:
  - Sources/GGLabAppRuntime/Demo/DemoManager.h::DemoManager
  - Sources/GGLabAppRuntime/Demo/DemoBase.h::DemoBase
ReviewPaths:
  - Sources/GGLabAppRuntime/ApplicationContentRegistration.cpp
```

## Lab runtime
Atlas ID: atlas.application.lab-runtime

- **Source owner:** `GGLabAppRuntime`, under `Sources/GGLabAppRuntime/Lab`. Concrete
  Labs are WinApp sessions under `Sources/WinApp/Application/Lab`, hosted by the
  `DemoLabHost` Demo.
- **Responsibility:** register Labs, consume Lab commands at the safe point, prepare
  and activate sessions, retire replaced ones and route parameter impacts.
- **Interface:** [LabSessionBase.h](../../../Sources/GGLabAppRuntime/Lab/LabSessionBase.h)
  (session callbacks and parameter hooks),
  [LabInterfaces.h](../../../Sources/GGLabAppRuntime/Lab/LabInterfaces.h)
  (`ILabControl`) and [LabCatalog.h](../../../Sources/GGLabAppRuntime/Lab/LabCatalog.h)
  (`LabRegistration`).
- **Implementation:** [LabRuntime.cpp](../../../Sources/GGLabAppRuntime/Lab/LabRuntime.cpp)
  and [LabRuntimeParameters.cpp](../../../Sources/GGLabAppRuntime/Lab/LabRuntimeParameters.cpp).
- **Lifetime:** `DemoLabHost` owns the `LabRuntime`, which owns the active, pending
  and retiring sessions. Each session owns its World, cameras, pipeline and asset
  owner scope.
- **Boundary:** Lab IDs are persisted ([AGENTS.md](../../../AGENTS.md#gpu-shader-and-lab-invariants));
  [Content retirement](../Frame_Execution_Map.md#content-retirement).
- **Verification:** `GGLabAppRuntimeTests` suite `gpu-fault-lifecycle`, including
  `RunLabSessionRetirementScenario` and `RunRebuildParameterScenario`; WinApp
  `napa-voxel` and `app-content-registration`; `--lab` GPU runs.

```text
Anchors:
  - Sources/GGLabAppRuntime/Lab/LabRuntime.h::LabRuntime
  - Sources/GGLabAppRuntime/Lab/LabSessionBase.h::LabSessionBase
  - Sources/WinApp/Application/Demo/DemoLabHost.h::DemoLabHost
ReviewPaths:
  - Sources/GGLabAppRuntime/Lab/LabCommandQueue.cpp
  - Sources/WinApp/Application/Content/DesktopApplicationContent.cpp
```
