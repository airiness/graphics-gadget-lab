# Boundary and Drift Guide
Atlas ID: atlas.maintenance.boundaries-freshness

- VerifiedAgainst: `3be3b56bdb224432ef650465310712194368e397`

## Authority and baselines

Project membership and source establish ownership and behavior. Interface contracts,
[AGENTS.md](../../AGENTS.md), and the contracts in the
[frame](Frame_Execution_Map.md#contracts) and
[composition](Runtime_Boundaries_Map.md#contracts) maps define their invariants.
Maps and routing remain usable from this repository alone.

`Atlas ID`, `VerifiedAgainst`, `Anchors` and `ReviewPaths` support the read-only
navigation and impact checker. `VerifiedAgainst` identifies the reviewed source
commit; documentation-only edits do not advance it.

## Refresh one affected entry

1. Follow the entry's source anchors, Public interfaces and implementation.
2. Check ownership, call/data flow and resource lifetimes against the local contract.
3. Update the affected map and its links. Resolve source/contract disagreements in
   the owning domain.
4. Advance `VerifiedAgainst` for the source reviewed. Keep review findings in code
   review rather than adding a work log to the map.

## Checks

From the repository root:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CheckArchitectureAtlas.ps1 -RootDir . -BaseRef origin/main
powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CheckArchitectureAtlas.ps1 -SelfTest
powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/ValidateProjectBoundaries.ps1 -RootDir .
```

The Atlas helper reports local link/anchor errors, baseline ancestry, external
design-document dependencies and affected entries. Inspect the report: exit 0
means a report was produced. `-AsJson` returns the same data for tools. The boundary
validator separately checks project ownership and include rules.

```text
Anchors:
  - Scripts/ValidateProjectBoundaries.ps1
  - Scripts/CheckArchitectureAtlas.ps1
  - AGENTS.md
ReviewPaths:
  - .agents/skills/gglab-architecture/**/*.md
```
