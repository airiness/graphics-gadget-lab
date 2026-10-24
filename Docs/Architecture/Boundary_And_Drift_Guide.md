# Boundary and Drift Guide
Atlas ID: atlas.maintenance.boundaries-freshness

- Question: what does a checked map establish, and when must it be refreshed?
- VerifiedAgainst: `c5fc8eec360cab77227714865d2d39e7d316fbb2`
- Refresh triggers: represented boundaries, evidence semantics, refresh ownership
  or the local verification workflow changes.

## Authority and baselines

Source and project membership show what the implementation does. Local interface
contracts, [AGENTS.md](../../AGENTS.md) and the map
[Contracts](Frame_Execution_Map.md#contracts) state what it is required to do. A
passing test shows only the cases it executed. When source and a rule disagree,
resolve it with the subsystem owner as either an implementation fix or an explicit
rule change.

`VerifiedAgainst` is the source commit that was reviewed, not the commit that
contains the map.

### Evidence labels

In reviews, findings and handoffs, label a material claim whose support is not
obvious from its link:

| Label | Evidence |
| --- | --- |
| `[Observed]` | Directly checked source, project metadata or recorded result at a stated revision |
| `[Documented]` | A local rule, interface contract or map statement, retaining its status and scope |
| `[Inferred]` | A conclusion from named evidence, with reasoning and uncertainty stated |
| `[Unverified]` | The evidence needed for the claim has not been checked |

## Refresh one affected entry

1. Note the reviewed source revision and any staged, unstaged or untracked changes.
   After a rebase or merge, compare the represented source and contracts before
   moving the baseline.
2. Read the entry's question, refresh triggers, `Anchors` and `ReviewPaths`. The
   paths point to likely candidates only: an internal edit may leave the meaning
   intact, and a changed caller outside the list may still change it.
3. Follow the Public interfaces and the necessary Private path.
4. Compare the entry's statements with the canonical rule and current source,
   including outcome and retention conditions when every path and symbol still
   exists. Repair the entry and its inbound links.
5. Record the result in the PR, using the terms below. Advance `VerifiedAgainst`
   only on entries you reviewed.

| Result | Action |
| --- | --- |
| `CheckedUnchanged` | Keep the text; record the reviewed revision as `VerifiedAgainst` |
| `MapStale` | Repair the entry, or record it under [pending refreshes](#pending-refreshes) |
| `ContractConflict` | Resolve with the subsystem owner before changing the rule or the map |
| `EvidenceIncomplete` | Keep the old baseline and name the missing evidence |

Each `Atlas ID` line names the entry it sits under; links use heading anchors
instead. An `Anchors` / `ReviewPaths` block belongs to the innermost enclosing
section that declares an `Atlas ID`.

## Pending refreshes

If a refresh must wait, add `- Status: MapStale, see pending refreshes` to the
entry's header list, keep its old `VerifiedAgainst` and add a row here. Remove both
when the refresh lands.

| Entry | Source commit | Expected change | Owner |
| --- | --- | --- | --- |
| None | | | |

## Checks

From the repository root, with the PR target branch available locally:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CheckArchitectureAtlas.ps1 -RootDir . -BaseRef origin/main
powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CheckArchitectureAtlas.ps1 -SelfTest
```

The helper checks local links, heading anchors and anchor file/symbol text, and
whether each `VerifiedAgainst` commit is an available ancestor of `HEAD`. It lists
as `ReviewRequired` the entries whose anchors or review paths changed since the
merge base or in the working tree; review each with the procedure above, since the
list is not a staleness verdict. Missing history or an unavailable merge base is
reported as `EvidenceIncomplete`, never fetched. A produced report exits 0 even
with findings, and `-AsJson` emits the same data.

The existing validator owns boundary rules; report its result separately from
map freshness:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/ValidateProjectBoundaries.ps1 -RootDir . -SelfTest
powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/ValidateProjectBoundaries.ps1 -RootDir .
```

```text
Anchors:
  - Scripts/ValidateProjectBoundaries.ps1
  - Scripts/CheckArchitectureAtlas.ps1
  - AGENTS.md
ReviewPaths:
  - .agents/skills/gglab-architecture/**/*.md
```
