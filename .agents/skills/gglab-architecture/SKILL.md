---
name: gglab-architecture
description: Orient gglab architecture investigations and cross-module changes using local task routes, contracts and source evidence. Use for project ownership, public interfaces, frame lifecycle and RenderGraph/RHI boundaries; ordinary formatting and text corrections need no orientation.
---

# GGLab architecture

## Route

Start with the matching row in [task routing](references/task-routing.md), one
map section and the cards needed for the question; the
[module catalog](../../../Docs/Architecture/Module_Catalog.md) lists every card. Use
the [overview](../../../Docs/Architecture/README.md) when the owner is unclear.

Identify the owning project, its Public interface and its implementation entry
separately.

## Verify

Check the current revision and relevant staged, unstaged and untracked work before
reusing a map's baseline. Read the applicable local contract and trace the task's
call, data or lifetime path in current source. For a settlement change, follow its
AppRuntime feedback and resource-retention consequences as well.

When a change may affect an Atlas entry, run the helper in the
[guide's checks](../../../Docs/Architecture/Boundary_And_Drift_Guide.md#checks) and
review each listed entry against source and contracts. In findings and handoffs,
mark material claims whose support is not obvious from a link with an
[evidence label](../../../Docs/Architecture/Boundary_And_Drift_Guide.md#evidence-labels):
`[Observed]`, `[Documented]`, `[Inferred]` or `[Unverified]`.

## Expand and continue

Read an adjacent module only to resolve a concrete ownership, lifetime,
compatibility or contract question. Stop orientation once you can name the owner,
public and implementation entries, relevant execution/lifetime path, governing
rules, verification route and any unresolved question material to the task.
Then continue the requested review or implementation under the repository rules.

If a route is missing or stale, reconstruct the affected path from current source
and contracts; refresh only reviewed entries and record deferred work in
[pending refreshes](../../../Docs/Architecture/Boundary_And_Drift_Guide.md#pending-refreshes).
Report material contract gaps to the subsystem owner. Use source inspection for
orientation; select builds and GPU checks for the task's verification.
