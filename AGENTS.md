# AGENTS.md

Graphics Gadget Lab (gglab) is a Windows graphics research project using C++20,
Direct3D 12, Vulkan, DXC, HLSL and ImGui. This file contains durable constraints
and task-specific entry points. Keep status, roadmaps and migration history out
of this file.

## Scope and completion

Complete the requested change and its relevant verification. Routine local
edits, metadata generation, builds and tests within the requested scope do not
need a separate approval at each step. Resolve routine implementation choices
from the repository and task context; ask when a missing decision materially
changes the requested behavior, architecture or scope. Existing user
authorization continues to apply, and explicit user instructions take priority
over repository workflow defaults.

Preserve unrelated working-tree changes. Staging, committing, amending, resetting,
rebasing, force-pushing or otherwise modifying Git history require an explicit
user request; preparing a change does not authorize those actions.

## Ownership and dependency boundaries

Visual Studio project membership is the source ownership authority. Every
first-party C++ source and header has exactly one owning project; never compile
the same first-party `.cpp` in multiple projects. Keep physical paths, include
visibility and project membership consistent.

- `WinApp` owns native startup and runtime inspection. `WinAppHost` owns Windows
  platform hosts/adapters, concrete Labs and DevTools. `WinAppTests` owns their
  regression cases and links the host implementation; product targets do not
  depend on test targets or include roots.
  `GGLabAppRuntime` owns host-neutral Application lifecycle, services
  and frame orchestration, without depending on WinApp content.
- `GGLabRuntime` owns reusable scene, graphics, RHI, rendering and runtime
  diagnostics. It must not depend on WinApp, WinAppHost, WinAppTests, AppRuntime
  or DevTools content.
  Ordinary consumers use `Sources/GGLabRuntime/Public` with the
  `GGLabRuntime/...` include prefix. Public headers must not expose Private or
  legacy implementation paths; do not restore Runtime Private include access to
  WinApp, WinAppHost or AppRuntime. Keep privileged access confined to explicitly declared
  qualification/test targets and their documented dependency closure.
- `GGLabFoundation` is Tier-0: no dependencies on Application, Runtime, Graphics,
  RHI, RenderGraph or Shader Toolchain. Its public contracts use
  `GGLabFoundation/...` and do not expose private implementation-library types.
  Foundation Private access belongs only to Foundation itself.
- `NapaVoxelCore` remains host-independent and C++20-compatible, without GGLab,
  DirectX, Win32, ImGui or Unreal headers or GGLab assertion/logging dependencies.
- Shader Toolchain and `ShaderToolchainTests` remain independent of Runtime/RHI
  implementations. Cross-domain shader tests belong to the explicitly
  privileged `ShaderRuntimeIntegrationTests` target. Standalone Vulkan hardware
  qualification belongs to `GGLabVulkanQualification`; normal WinApp must not
  acquire its qualification sources, `ShaderToolchainCore` linkage or DXC imports.
- Backend-neutral graphics contracts and recipes belong in the RHI or another
  neutral layer; native DX12/Vulkan types and translation belong in backend
  implementations. DevTools should consume immutable diagnostics snapshots or
  narrow query/control APIs instead of exposing renderer internals.

Keep application behavior in its owning domain. Enforce dependency direction
with compiler include visibility as well as the boundary validator.

The [Architecture Atlas](Docs/Architecture/README.md) maps the project layers, the
frame path, render pass composition and Lab lifetime to their source entry points
and owning tests. Use
[gglab-architecture](.agents/skills/gglab-architecture/SKILL.md) for architecture
orientation and cross-module changes; its task table selects the relevant map,
Public interface and implementation path.

## GPU, shader and Lab invariants

For GPU changes, follow ownership through submission, fences, frames in flight,
resource states, descriptors, transient allocation and cancellation before
submission. Readback requires GPU completion; retain the backing resource and
mapping until the consumer finishes.

The Atlas [frame contracts](Docs/Architecture/Frame_Execution_Map.md#contracts)
detail how these rules apply to Renderer settlement order, settlement-time failures
and teardown; keep both texts consistent when either changes.

RenderGraph owns dependencies, culling, execution order, transient lifetimes
and barriers for its managed resources. Passes declare all managed reads and
writes. Imported/exported resources need explicit ownership and state contracts.
Fix ordering through those contracts rather than reaching into another pass's
private resources. Justify manual barriers and consider whether the general
barrier-planning rule needs correction. For compiler changes, inspect dependency
analysis through resource release and barrier planning as one connected path.
The Atlas [composition contracts](Docs/Architecture/Runtime_Boundaries_Map.md#contracts)
detail pass ownership and the lifetime of imported resources; keep both texts
consistent when either changes.

Keep shader compilation policy and DXC argument construction authoritative in
the Shader domain; avoid new Renderer/RHI coupling to host-side compilation.
Durable recipe, artifact and cache identities also belong to Shader: runtime
CRC/FNV/StringId values are not substitutes for persisted identity. Version
persisted schemas, ABIs and process protocols in the owning domain, without a
global Foundation version for unrelated contracts.

Shader sources and metadata live under `Shaders`; shared HLSL includes use
`.hlsli`. Each shader source, include and metadata file has one feature-domain
project owner, including CPU/HLSL shared headers. Compiler-tool and deployment
consumers do not determine source ownership; see the Atlas
[shader ownership map](Docs/Architecture/Repository_Module_Map.md#shader-source-ownership).
Keep CPU/GPU layouts synchronized, including field order, alignment,
padding, stride and relevant `static_assert`s. Do not rely on implicit C++
padding for GPU uploads. Shader compilation and correct rendering are separate
verification claims.

Lab IDs in `gglab.lab.*` are persisted identifiers. Apply Lab switches and
rebuild-triggering changes through the LabRuntime safe command path. Preserve
session ownership of worlds, cameras, pipelines and fence-dependent work; use
immediate updates only when the state can safely change in place. The Atlas
[Content retirement](Docs/Architecture/Frame_Execution_Map.md#content-retirement)
rule details how long replaced and pending content is kept; keep both texts
consistent when either changes.

## Source and generated files

- Use C++20 and include semantic dependencies directly; PCH contents are not an
  include contract. Do not explicitly include removed or build-injected PCH
  headers in ordinary sources.
- In ordinary C++ code, end statement-like and declaration-like macro
  invocations with a semicolon at the call site, even if the expansion
  already supplies one. Expression-like macros follow the surrounding
  expression syntax.
- Use braces for every `if`, `else`, `for` (including range-based loops), `while`
  and `do-while` body in first-party C++ and HLSL, even for a single statement.
  Keep conventional `else if` chains, with braces around each branch body.
- Apply the [gglab-format](.agents/skills/gglab-format/SKILL.md) conventions while
  editing first-party C++ and HLSL. Use 100 columns as a soft target, prioritizing
  readability and semantic grouping. Detailed layout rules and examples belong
  in that skill.
- Follow neighboring naming and layout. Use `gglab` for GGLab code and
  `napa::voxel` for Napa core code. Use lasting semantic names instead of phase,
  commit or cache-epoch labels in durable identifiers, filenames and comments.
  Name polymorphic base classes with a `Base` suffix when following the existing
  project convention. Prefer existing assertions and logging within the owning
  domain.
- Follow `.editorconfig` and `.gitattributes`: UTF-8 without BOM and CRLF by
  default, preserving per-file exceptions. Restore this form before handoff;
  `Scripts/OpenGGLabVS.bat` normalization must not create follow-up diffs.
  Preserve the exact LF bytes of `Shaders/Tests/Generated/*.hlsli` fixtures.
  Review C++ and HLSL layout manually as described in gglab-format.
  Preserve Visual Studio project files' repository-specific encoding and layout;
  use the repository scripts for repair or regeneration.
- Preserve the `Externals/Vender` spelling. Edit `Externals` only for a task
  concerning a vendored dependency. Keep build products, IDE state and shader
  caches out of commits.
- Third-party assets require known redistribution rights and synchronized
  source, rights-holder, license and attribution entries in the nearest
  `THIRD_PARTY_NOTICES.md`. Large assets need a runtime, Lab or validation use.

Run the following commands from the code repository root when their trigger
applies. Scripts own generated `.vcxproj.filters` and shader project entries;
do not hand-edit their generated mappings.

| Trigger | Command |
| --- | --- |
| C++ files added/moved or project membership changed | `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/GenerateProjectFilters.ps1 -RootDir .` |
| Verify affected filter metadata before handoff | `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/GenerateProjectFilters.ps1 -RootDir . -Check` |
| Ownership, project files, include paths or platform boundaries changed | `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/ValidateProjectBoundaries.ps1 -RootDir .` |
| Shader sources/metadata added, removed, moved or reassigned to another project | `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/SyncShadersToVS.ps1 -RootDir .` (also regenerates filters) |

## Build and verification

After implementing first-party C++ or HLSL changes, complete the scoped
[gglab-format](.agents/skills/gglab-format/SKILL.md) pass before verification
builds. Diagnostic compiles during implementation may precede this pass. Revisit
later code edits before final verification; unchanged code needs no repeated
formatting pass for each build configuration or test invocation.

Use [README.md](README.md) for setup and the
[CI workflow](.github/workflows/build.yml) for executable test commands and the
build matrix. List scripted verification suites with
[RunVerification.ps1](Tests/Verification/RunVerification.ps1) `-List`. Use the
owner's Visual Studio/MSVC toolchain. Run builds from the repository root
in a shell with x64-hosted MSBuild available
(`MSBuild/Current/Bin/amd64/MSBuild.exe`); outputs are under
`Build/Output/x64/<Configuration>/`.

Pass `/p:PreferredToolArchitecture=x64` to keep the compiler host consistent
with Visual Studio. For direct `.vcxproj` builds, also pass
`"/p:SolutionDir=$gglabSolutionDir"` using the absolute repository root with a
trailing backslash. Keep these settings consistent between command-line and
IDE builds so they share the same incremental build state.

```powershell
$gglabSolutionDir = (Get-Location).Path.TrimEnd('\') + '\'
msbuild GraphicsGadgetLab.sln /m /p:Configuration=Debug /p:Platform=x64 /p:PreferredToolArchitecture=x64
msbuild GraphicsGadgetLab.sln /m /p:Configuration=Release /p:Platform=x64 /p:PreferredToolArchitecture=x64
```

Select checks according to the change:

- Documentation-only changes: check the diff, referenced paths/commands and
  formatting. A C++ rebuild or GPU run is unnecessary.
- Focused code changes: build the owning target and run its affected suites;
  cover each affected domain when a change crosses boundaries. Add regression
  coverage for changed behavior where useful, not tests that merely mirror an
  implementation or trivial text edit.
- Dependency-boundary changes: build affected libraries directly, not only
  through the solution. For WinApp include/PCH changes, use the no-PCH gate:
  `msbuild Projects/WinApp/WinApp.vcxproj /m /p:Configuration=Debug /p:Platform=x64 /p:PreferredToolArchitecture=x64 "/p:SolutionDir=$gglabSolutionDir" /p:GGLAB_USE_PCH=0`.
  When changing ownership or dependency closure of Vulkan qualification or the
  shader test boundaries, build the affected `GGLabVulkanQualification`,
  `ShaderToolchainTests` and/or `ShaderRuntimeIntegrationTests` projects directly
  in both Debug and Release.
- Graphics/runtime changes: run the relevant smoke test when supported. For
  deterministic Lab selection, use
  `GraphicsGadgetLab.exe --rhi <dx12|vulkan> --lab <stable-lab-id>`.
  Exercise both backends for shared rendering changes; inspect assertions,
  validation output, shader failures, lifetime/synchronization errors and the
  visual result. Use the repository's `gglab-frame-capture` skill at
  `.agents/skills/gglab-frame-capture/SKILL.md` for hidden capture and verification.
  Qualification's `--self-test` checks headless contracts;
  `GGLabVulkanQualification.exe [--adapter <index|identity-prefix>]` performs
  hardware qualification. Headless tests do not establish GPU presentation or
  visual correctness.

Do not run IDE and command-line builds concurrently against the same outputs,
or link while the target executable is running. Fix failures caused by the
change without weakening assertions, validation or tests. Once relevant checks
pass, expand or repeat them only for a new change, failure or unresolved concern.
Report unavailable SDK/GPU/environment gates and distinguish completed checks
from untested paths.

## Change discipline

Before changing ownership, lifetime, synchronization or persisted contracts,
inspect the complete relevant call/data path rather than patching only the
immediate symptom. Prefer focused changes over unrelated subsystem-wide
refactors.

Do not silently weaken assertions, validation, error handling or test coverage
to make a failure disappear. When fixing a non-obvious correctness bug, record
the violated invariant in code or tests where that improves future
maintainability.

Before handoff, review the diff for unrelated modifications, confirm new or
moved first-party files have one owning project, regenerate or check project
metadata when ownership changed, and report known limitations and untested
paths.

## Language and handoff

Use English for repository documentation, source comments, identifiers, logs,
application text, GitHub content and commit messages. Communication with the
owner may use Chinese with English technical terms.

Before handoff, inspect the final diff and finish the applicable checks above.
Report the result, verification and material limitations concisely. Provide a
proposed English Conventional Commit message with an imperative
`type(scope): summary` subject in its own plain fenced code block; keep test
results outside that block. If independently reviewable changes are
intentionally split, provide one proposed message per slice and identify the
corresponding scope outside the code blocks. Proposing a message does not
authorize a commit.
