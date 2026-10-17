---
name: gglab-format
description: Review and normalize first-party gglab C++ and HLSL layout after implementation, before verification builds, or for requested formatting work. Preserve task scope and fixture data.
---

# GGLab format

Apply the repository's layout conventions while editing, then complete a scoped
formatting pass before building the completed implementation for verification.
Read [Layout rules](references/layout-rules.md). Consult the relevant sections
of [Examples](references/examples.md) for the constructs being changed.

## When to run

- Use this workflow after a batch of first-party C++ or HLSL changes, including
  headers, tests and test helpers, before its verification build.
- Use it for explicitly requested formatting cleanup or audits. Honor a
  review-only request by reporting findings without editing.
- Documentation-only work and read-only investigations need no source formatting.
  A diagnostic compile during implementation may precede this pass. After later
  code edits, revisit the affected regions before final verification; unchanged
  code needs no repeated pass for each build configuration or test invocation.

## Establish the editing scope

Identify the paths and regions changed by this task, including new untracked
source files. Inspect the working tree and index to distinguish them from the
owner's existing changes. Git's complete dirty-file list is not the formatting
scope. Leave staging and unrelated changes untouched.

For an existing file, format the affected regions and enough surrounding code to
keep the enclosing construct consistent. A new file can be formatted as a whole.
If the available formatter operates on a whole file, use an isolated copy with
the same effective configuration and apply only reviewed changes in scope.
Repository-wide cleanup requires an explicitly requested broader scope.

Exclude vendored code, build outputs and generated fixtures unless the task
specifically concerns them. In particular, preserve the byte-pinned LF files in
`Shaders/Tests/Generated/*.hlsli`.

## Format and review

Use the root `.editorconfig` and `.gitattributes`. Use Visual Studio's native C++
formatter for C++ and the HLSL language service for shaders. Do not run HLSL
through a C++ formatter, or substitute clang-format or another formatting engine.
Do not change shared IDE preferences or repository formatting settings to make
one formatting pass succeed.

After native formatting, inspect the affected constructs against the layout
rules, particularly:

- Control bodies, scoped `case` branches and macro call-site semicolons.
- Continuation levels, nested ternaries and their branch initializers.
- Lambda initializer continuations versus lambdas passed as call arguments.
- Complete declarations, readable calls, arrays, repeated records and JSON fields.
- HLSL attributes and casts, comments inside conditional compilation, and code
  following multiline raw strings.

Repair formatter regressions manually. Stable formatter output is not proof that
it follows these conventions. Finish with the reviewed layout; do not repeatedly
format away the corrections. If the native formatter is unavailable, make scoped
manual repairs and report that limitation while continuing applicable checks.

Preserve token order, literal bytes, comments and preprocessor boundaries during
whitespace-only edits. Inserting required control braces or correcting an HLSL
cast adds syntax: inspect those repairs separately, preserving expression types,
operand boundaries and control flow. Do not use blanket text substitutions that
can rewrite comments, raw strings, macro expansions or ambiguous expressions.

## Finish before verification

Review the formatting diff separately from the intended behavior changes. Check
for unintended edits outside scope, repeated indentation growth, and artificial
wrapping introduced only to meet the soft column target. Preserve neighboring
pointer/reference spacing and existing readable layouts.

Restore UTF-8 without BOM and CRLF for edited source files, honoring per-file
exceptions. Check new files as well as tracked diffs. Run
`git diff --check -- <task-paths>` for the tracked changes; this checks whitespace
errors, not all layout rules. For a large formatting-only pass, compare lexical
tokens and literal contents before and after, accounting explicitly for any
deliberate syntax repairs.

If HLSL syntax repairs affect a registered non-finite site, review the source
change before updating its `bodySha256` in `Shaders/Metadata/NonFiniteSites.json`.
These identities ignore whitespace, not tokens such as cast parentheses or
control braces. Regenerate the shared identifiers with
`python Scripts/GenerateNonFiniteSites.py`, then run
`python Scripts/GenerateNonFiniteSites.py --check`,
`python Scripts/ValidateNonFiniteSites.py --self-test` and
`python Scripts/ValidateNonFiniteSites.py` from the repository root. Do not
weaken identity validation or refresh unreviewed hashes to make a check pass.

Once the layout is final, run the owning builds and affected tests required by
the root `AGENTS.md`. Include the formatting scope, relevant verification and
any formatter limitation in the handoff. This skill is an agent workflow; it
does not install a build hook or establish GPU or visual correctness.
