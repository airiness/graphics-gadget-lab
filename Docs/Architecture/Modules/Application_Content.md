# Application Content Cards

- Question: which application module owns frame orchestration and content hosting,
  and where are its interfaces?
- VerifiedAgainst: `e8d08f13710eb206ea757292293cd327cebdc510`
- Refresh triggers: a card's source owner, Public interface, lifetime, linked
  contract or owning tests change.

Each card has one home here; the [module catalog](../Module_Catalog.md) lists all
cards. Contracts stay in their maps, and cards link to them.

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
