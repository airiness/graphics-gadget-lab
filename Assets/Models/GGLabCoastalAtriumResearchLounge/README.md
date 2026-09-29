# Coastal Atrium with Research Lounge

This original project asset is the Coastal Atrium with a two-place Research
Lounge beneath the pergola. The glTF Separate bundle contains 109 meshes,
115 nodes, nine opaque materials, four cameras, one Sun reference, one binary
buffer and nine PNG textures. Keep the `.gltf`, `.bin` and `Textures/` directory
together.

The editable source is `GraphicsGadgetLabContent/Scenes/GGLabCoastalAtrium/GGLabCoastalAtrium.blend`
at Content revision `df187bb492f1a5b6b519eb114d14debeb8eeb2f9`. Geometry and
textures are original project work; no third-party assets are used. Blender
5.1.1 and `GraphicsGadgetLabContent/Scripts/export_gltf.py` produced this bundle.

| Artifact | SHA-256 |
| --- | --- |
| Source `.blend` | `0a5572c81478c5a42a90462a600f6d72e795f25e7a5d1db78ec429c902cc4a47` |
| `GGLabCoastalAtrium.gltf` | `67d6a17f1ad2db6387c2265172e124b03bc1ad39823c259eef2d2d7e84f2e987` |
| `GGLabCoastalAtrium.bin` | `7733c3c4ffebd9bb6f9fcb54ccd6ed2c79e14743ca100a9790a20a176a06c7e6` |

To reproduce from the Content repository root:

```powershell
$blender = 'C:/Program Files/Blender Foundation/Blender 5.1/blender.exe'
& $blender --background --factory-startup --python-exit-code 1 --python Scripts/export_gltf.py -- `
  --input Scenes/GGLabCoastalAtrium/GGLabCoastalAtrium.blend `
  --output ../GraphicsGadgetLab/Assets/Models/GGLabCoastalAtriumResearchLounge/GGLabCoastalAtrium.gltf
```

The existing Demo loads `Assets/Models/GGLabCoastalAtrium/GGLabCoastalAtrium.gltf`.
This bundle is available for the material integration work; installing it in
the Demo and recording a new visual baseline require separate Runtime checks.
