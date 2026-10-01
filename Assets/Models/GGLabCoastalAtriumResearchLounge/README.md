# Coastal Atrium with Research Lounge

This original project asset extends the Coastal Atrium with a two-place coated
Research Lounge and a brushed-aluminum frame beneath the pergola. The glTF
Separate bundle contains 109 meshes, 115 nodes, nine opaque materials, four
cameras, one Sun reference, one binary buffer and nine PNG textures. Keep the
`.gltf`, `.bin` and `Textures/` directory together. No third-party assets are used.

## Source and identity

The editable source is
`GraphicsGadgetLabContent/Scenes/GGLabCoastalAtrium/GGLabCoastalAtrium.blend`
at Content revision `63a90291e62df30f96943f3f2d6cd1e11f52d87c`. Blender 5.1.1 and
`GraphicsGadgetLabContent/Scripts/export_gltf.py` produced this bundle.

| Artifact | SHA-256 |
| --- | --- |
| Source `.blend` | `664e0e36d301a3f653051c3ce17d837a8eb89e94ad85fbc671d29362a16325aa` |
| `GGLabCoastalAtrium.gltf` | `09bd2dda39f8b17fc2ea7243d5c7ef6a6e642da687cfd936bedb53941fff4685` |
| `GGLabCoastalAtrium.bin` | `02c353a5f124389adf047e40966d572bac32a395489c491e26a89b19a9c8454c` |

The shell retains clearcoat factor 0.82 and roughness 0.11. Its eight frame
parts use `MAT_LoungeBrushedAluminum`: linear RGB `(0.52, 0.56, 0.59)`, metallic
1, base roughness 0.28, anisotropy strength 0.78 and rotation 0. UV0 U follows
the length of each rail or leg. End bevels turn with the surface; the frame has
no normal map or mirrored UV. Cushions use opaque base materials. Existing
architecture and all nine PNGs are unchanged.

## Reproduce and validate

Run from the Content repository root:

```powershell
$blender = 'C:/Program Files/Blender Foundation/Blender 5.1/blender.exe'
& $blender --background --factory-startup --python-exit-code 1 --python Scripts/export_gltf.py -- `
  --input Scenes/GGLabCoastalAtrium/GGLabCoastalAtrium.blend `
  --output ../GraphicsGadgetLab/Assets/Models/GGLabCoastalAtriumResearchLounge/GGLabCoastalAtrium.gltf
& $blender --background --factory-startup --python-exit-code 1 --python Scripts/validate_research_lounge.py -- `
  --gltf ../GraphicsGadgetLab/Assets/Models/GGLabCoastalAtriumResearchLounge/GGLabCoastalAtrium.gltf
```

The exporter does not save the source. Content checks cover factors, frame
UV/tangent direction, geometry, cameras and round-trip import. The original
Content repeated exports also produced identical JSON, buffer and image bytes.

## Runtime entry

From the code repository root:

```powershell
./Build/Output/x64/Debug/GraphicsGadgetLab.exe --demo atrium --rhi dx12
./Build/Output/x64/Debug/GraphicsGadgetLab.exe --demo atrium --rhi vulkan
./Build/Output/x64/Debug/GraphicsGadgetLab.exe --self-test app-content-registration
```

`--demo atrium` loads this bundle through the production asset preparation path.
Use `WinApp` and `ShaderCompiler` outputs built from the same code revision.
The four existing camera profiles are unchanged. Restore a reference camera,
then set Manual EV100 15 for physical World Sun / Physical Sky checks; camera
restoration retains the legacy zero-EV reference. The exported Sun and Blender
preview World do not define Runtime lighting units or IBL.

The earlier [Atrium asset](../GGLabCoastalAtrium/README.md) and its baseline
captures retain their source identities. Installing this bundle does not replace
those screenshots. Runtime tests cover imported frame factors and shell
clearcoat; matching World Sun/Physical Sky images on DX12 and Vulkan remain
separate visual acceptance evidence.

The installed bundle passed Content source/export/round-trip checks and the
180-check `app-content-registration` suite in both Debug and Release. Bounded
Debug DX12 and Vulkan launches completed model/texture GPU uploads, activated
the Atrium Demo and reached the first production submit/present transaction.
That startup evidence does not establish that its materials rendered correctly;
no new Runtime captures were taken.
