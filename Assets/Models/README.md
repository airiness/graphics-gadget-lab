# Runtime asset bundles

This repository distributes the exported assets needed by its Labs, Demos and
import self-tests. Keep each glTF document, its adjacent binary buffer and all
referenced textures together. Relative glTF URIs resolve within the bundle.
Editable authoring sources and generation tools are maintained separately and
are not required to build, load or validate the installed Runtime assets.

The README beside each bundle describes its purpose, material/coordinate
contracts, file identities and Runtime entry. Any authoring-source fingerprint
is provenance only; it does not designate a file shipped in this repository.
Installed-file hashes identify the actual Runtime inputs independently of
authoring history. Original project fixtures contain no third-party content;
third-party assets retain their own notices and redistribution requirements.

The current `--demo atrium` uses [Coastal Retreat](GGLabCoastalRetreat/README.md).
Earlier Atrium and Research Lounge bundles remain available for their recorded
capture baselines and import checks.

## Verify installed content

Build WinApp and ShaderCompiler from the same code revision using the setup in
the repository [README](../../README.md). From the repository root, run:

```powershell
./Build/Output/x64/Debug/GraphicsGadgetLab.exe --self-test app-content-registration
```

The suite imports installed fixtures through the production asset pipeline and
checks their geometry, texture semantics, material inputs and registered views.
Use the individual asset README's DX12/Vulkan launch commands for presentation
and visual checks. CPU import tests do not establish GPU rendering correctness.

For a frozen rendering comparison, use that capture record's manifest,
exported assets and rendering settings. The [Coastal Atrium baseline](../Media/GGLabCoastalAtrium/Baseline1/CAPTURE.md)
links the frozen manifest. `Scripts/ValidateRenderingBaseline.ps1` accepts
baseline schema version 2 and checks Runtime asset and screenshot hashes within
this repository. The [capture evidence format](../Media/CAPTURE_EVIDENCE.md)
describes investigation records. Authoring preservation checks belong to the
authoring workspace.
