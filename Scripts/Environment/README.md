# Environment publisher

Implementation and fixtures are in this repository. The reviewable contract is
owned by the docs repository:
`../../../GraphicsGadgetLabDocs/GGLab_Environment_Publication_Contract.md`.
Status: implemented producer proposal, pending owner review before Editor import.

Use Python 3.12+ on Windows, Git, a built x64 deployment, and the installed Vulkan
SDK (VULKAN_SDK, falling back to the project's pinned 1.3.296.0 installation).
Read `bootstrap.json` for discovery. Send one UTF-8 JSON object on stdin:

```powershell
'{"requestVersion":1,"operation":"discover","repositoryRoot":"D:/Grezzo/Ruisong/GraphicsGadgetLab","searchRoots":["Build/Output"]}' | python Scripts/Environment/publish_environment.py
'{"requestVersion":1,"operation":"publish","repositoryRoot":"D:/Grezzo/Ruisong/GraphicsGadgetLab","deployment":"Build/Output/x64/Debug","destination":"D:/GGLab/E1","cancelFile":null}' | python Scripts/Environment/publish_environment.py
'{"requestVersion":1,"operation":"verify","environmentRoot":"D:/GGLab/E1"}' | python Scripts/Environment/publish_environment.py
'{"requestVersion":1,"operation":"init-state","environmentRoot":"D:/GGLab/E1","stateRoot":"D:/GGLab/S1"}' | python Scripts/Environment/publish_environment.py
```

For non-ASCII absolute paths, hosts should write UTF-8 directly to child stdin;
do not rely on legacy Windows PowerShell's pipeline encoding. Use short storage
paths because existing native artifact writers have Win32 path-length limits.

Discover/select the deployment before publishing; Debug in the example is not
an implicit selection policy. Publisher success means finalized closure, not
Editor registration or native readiness. Fresh final-path handshakes and exact
executable observation remain mandatory.

```powershell
python -m unittest discover -s Tests/Environment -v
python Tests/Environment/qualify_native.py --deployment Build/Output/x64/Debug --output Build/Output/EQ-new
```

The qualification output directory must not exist. It is disposable and includes
a private source mirror, native process logs and `evidence.json`; only that mirror
is renamed. The original checkout is never moved or changed. See fixtures for
strict reader and process failure expectations. Fixture executables/artifacts are
synthetic and must never be used to claim native compatibility.

See [Verification.md](Verification.md) for executed checks, durable native
evidence, limitations and the Editor handoff.
