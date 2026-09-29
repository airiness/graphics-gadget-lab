# Lighting Contract with Clearcoat

This original project reference scene contains reflectance, lighting sphere,
sun angle, roughness, IOR and clearcoat comparisons. The glTF Separate bundle
contains 38 meshes, 44 nodes, 30 materials, five cameras, one binary buffer
and one PNG coat-normal texture. Keep the `.gltf`, `.bin` and `Textures/`
directory together. No third-party assets are used.

The editable source is `GraphicsGadgetLabContent/Scenes/GGLabLightingContract/GGLabLightingContract.blend`
at Content revision `df187bb492f1a5b6b519eb114d14debeb8eeb2f9`.
Blender 5.1.1 and the Content export script produced the source glTF.
`Scripts/derive_lighting_ior_export.py` then removed Blender's specular
export trigger from the controlled IOR row, preserving the IOR and clearcoat
extensions, binary buffer and normal image.

| Artifact | SHA-256 |
| --- | --- |
| Source `.blend` | `e70e2891c29b68f67b60ec5e547a45e18e74863c1a2a98555d9b3fe7745e7649` |
| `GGLabLightingContract.gltf` | `a6ff60f04c944ae7c8c55f0a764007edf7feef66d70d65099e355907008f30b7` |
| `GGLabLightingContract.bin` | `fff9e41e4b1e4a8ed4262b23ed386f6609aa7414fd356e3cc713c8c6650e4c89` |
| `Textures/ClearcoatNormal.png` | `0a4f1c77443dad0809e268397dda2c71e41506035bc8d813f977a911b3ab5b38` |

To reproduce from the Content repository root:

```powershell
$blender = 'C:/Program Files/Blender Foundation/Blender 5.1/blender.exe'
& $blender --background --factory-startup --python-exit-code 1 --python Scripts/export_gltf.py -- `
  --input Scenes/GGLabLightingContract/GGLabLightingContract.blend `
  --output Exports/Checks/LightingContractGGLabHandoff/GGLabLightingContract.gltf
python Scripts/derive_lighting_ior_export.py `
  --input Exports/Checks/LightingContractGGLabHandoff/GGLabLightingContract.gltf `
  --output ../GraphicsGadgetLab/Assets/Models/GGLabLightingContractClearcoat/GGLabLightingContract.gltf
```

The existing Lab loads `Assets/Models/GGLabLightingContract/GGLabLightingContract.gltf`.
This bundle is available for material integration; installing it in the Lab
and recording a new visual baseline require separate Runtime checks.
