# Producer verification and Editor handoff

Recorded 2026-09-08; final native runs completed 2026-09-07 (Asia/Tokyo).
The contract is an implemented proposal pending owner review before Editor import.
This report is evidence, not a competing normative specification.

## Executed checks

- Debug and Release solution builds passed; the Debug application also built
  with `GGLAB_USE_PCH=0`. Final owning application rebuilds passed.
- Application self-tests passed: 186 checks per configuration.
- Shader toolchain tests passed: 235 checks per configuration.
- Debug Runtime tests passed: 621 checks.
- Python contract tests passed: 18 tests, including invalid versions, missing
  members, hash/identity mismatches, unsafe paths, reparse points, hardlinks,
  incomplete publication, duplicate/conflicting/concurrent publication,
  cancellation, failure recovery, and writable-state isolation.

The durable machine-readable snapshot is
[`qualification.json`](../../Tests/Environment/evidence/qualification.json).
Raw logs and deployments remain in ignored `Build/Output/EQ7` (Release) and
`Build/Output/EQ8` (Debug without PCH). Each configuration passed DX12 and Vulkan,
Surface Profile v1 and v2, with DevTools enabled. All eight runs performed a
fresh ordinary compile, Preview build, first production frame and Loaded
observation, an invalid build returning exit 4 while preserving last-good
pointer and observation, and successful recovery to attempt 3. Final closure
verification passed after execution. The snapshot includes exact executable
hashes, final-location handshakes, publication identities and observations.

Debug Vulkan validation was enabled using the published layer; both profiles
reported zero validation errors and warnings. Release Vulkan validation was
disabled, so its successful execution is not validation-layer coverage.
Debug logs contain the existing GameInput-unavailable diagnostic (0xD0000034);
the tested UI-only input path still rendered and observed Preview successfully.

## Isolation and limits

The runner publishes from its own disposable source mirror, then renames that
mirror so the publication's original source and deployment paths do not exist.
It subsequently uses only final published resources and external state for the
tested workflows. It never moves or changes the user's original checkout.
This is source-path isolation, not OS-wide denial of the original checkout;
there is no system-call tracing or pixel/screenshot comparison in this evidence.

An earlier long-root native artifact write failed before finalization; shorter
storage roots passed. Existing native Win32 artifact path limits still apply.
An initial missing startup UV texture was fixed by publishing both required UV
textures. Final EQ7/EQ8 runs supersede earlier exploratory runs. A restricted-token
isolation experiment failed during process initialization and is not counted as
successful qualification. Failed publication attempts retain private work/staging
directories for diagnosis and do not replace a usable destination.

Dedicated Shader Runtime Integration and standalone Vulkan Qualification suites,
Release Runtime tests, and other machines/driver versions were not executed.
Windows, GPU drivers and the system Vulkan loader remain platform prerequisites.
Debug CRT packaging is for development qualification, not a redistribution grant.

## Consumer entry points

Read the docs repository's `GGLab_Environment_Publication_Contract.md`, then
[`bootstrap.json`](bootstrap.json), [`README.md`](README.md), and the strict
[`publisher/reader`](publish_environment.py). Consume requestVersion 1 operations
`discover`, `publish`, `verify`, and `init-state`; discovery returns multiple
candidates and never selects one implicitly.

Use [`fixtures/index.json`](../../Tests/Environment/fixtures/index.json), its
positive/negative manifests, and the filesystem/process vectors for strict reader
tests. Fixtures are synthetic and cannot establish native compatibility. Use
[`qualify_native.py`](../../Tests/Environment/qualify_native.py) to reproduce native
evidence in a new output directory. The Editor must still prove exact executable
identity, re-read descriptors, and perform ordinary/Preview handshake and host/
Runtime observation at the final location. EnvironmentId does not replace them.
