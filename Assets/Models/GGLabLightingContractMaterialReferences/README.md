# Lighting Contract Material References

This original project reference scene contains reflectance, lighting sphere,
sun-angle, roughness, opaque dielectric IOR, clearcoat and anisotropy comparisons.
The glTF Separate bundle contains 45 meshes, 52 nodes, 36 materials, six cameras,
one binary buffer and two normal PNGs. It contains no lights. Keep the `.gltf`,
`.bin` and `Textures/` directory together. No third-party assets are used.

## Source and identity

The editable source is
`GraphicsGadgetLabContent/Scenes/GGLabLightingContract/GGLabLightingContract.blend`
at Content revision `63a90291e62df30f96943f3f2d6cd1e11f52d87c`. Blender 5.1.1 and
`Scripts/export_gltf.py` produced the direct export. The Content script
`Scripts/derive_lighting_ior_export.py` removed only Blender's specular export
trigger from the IOR row, preserving IOR, clearcoat, anisotropy, geometry and
both normal images. This bundle supersedes the former `GGLabLightingContractClearcoat`
handoff path; the original Lighting Contract asset remains available.

| Artifact | SHA-256 |
| --- | --- |
| Source `.blend` | `ce4e1ca18e3718cc3257123500f8d870926027609fe6d61126a80457fb864ce5` |
| `GGLabLightingContract.gltf` | `f4e9dc7a4368c83c6cbcf2f0dd38007cb038e39657785e61c7380a56e90f9d9d` |
| `GGLabLightingContract.bin` | `78c766e04fe97a16272bb1f66f439f18660abe7336a116ee61edbe4c25209c6e` |
| `Textures/BrushedNormal.png` | `0f1b57c0e91588d15aa303a679dbbb9ba9ed01a6cfbe58549dd64c0b888a1a5c` |
| `Textures/ClearcoatNormal.png` | `0a4f1c77443dad0809e268397dda2c71e41506035bc8d813f977a911b3ab5b38` |

## Controlled material cases

- IOR: 1.0, 1.33, implicit default 1.5, 1.7 and 2.0 on matched opaque dielectrics.
- Clearcoat: off, smooth, rough and independent normal map with scale 0.45.
- Anisotropy: strength off, strength 0.85 at 0/90/45 degrees, mirrored U at
  45 degrees, and a base normal map at scale 0.4 with the same 45-degree frame.

The anisotropy spheres share linear RGB `(0.48, 0.52, 0.56)`, metallic 1 and
base roughness 0.42. The unmirrored front tangent maps to runtime +X; mirrored
U maps it to -X while preserving the +Y bitangent. Normal images are linear
data on UV0. Exact geometry and exported direction contracts are recorded in
`GraphicsGadgetLabContent/Scenes/GGLabLightingContract/README.md`.

## Reproduce

Run from the Content repository root:

```powershell
$blender = 'C:/Program Files/Blender Foundation/Blender 5.1/blender.exe'
& $blender --background --factory-startup --python-exit-code 1 --python Scripts/export_gltf.py -- `
  --input Scenes/GGLabLightingContract/GGLabLightingContract.blend `
  --output Exports/Checks/LightingContractGGLabHandoff/GGLabLightingContract.gltf
python Scripts/derive_lighting_ior_export.py `
  --input Exports/Checks/LightingContractGGLabHandoff/GGLabLightingContract.gltf `
  --output ../GraphicsGadgetLab/Assets/Models/GGLabLightingContractMaterialReferences/GGLabLightingContract.gltf
& $blender --background --factory-startup --python-exit-code 1 --python Scripts/validate_lighting_contract.py -- `
  --gltf Exports/Checks/LightingContractGGLabHandoff/GGLabLightingContract.gltf `
  --derived ../GraphicsGadgetLab/Assets/Models/GGLabLightingContractMaterialReferences/GGLabLightingContract.gltf
```

The exporter does not save the source. Content checks cover geometry, materials,
normal PNGs, tangent handedness, six camera targets and round-trip import. The
original Content repeated exports produced identical JSON, buffer and images.

## Runtime entry and camera profiles

From the code repository root:

```powershell
./Build/Output/x64/Debug/GraphicsGadgetLab.exe --lab gglab.lab.lighting_contract --rhi dx12
./Build/Output/x64/Debug/GraphicsGadgetLab.exe --lab gglab.lab.lighting_contract --rhi vulkan
./Build/Output/x64/Debug/GraphicsGadgetLab.exe --self-test app-content-registration
```

In Lab Control, choose `Scene > Reference Scene > Extended Material References`.
Use `WinApp` and `ShaderCompiler` outputs built from the same code revision.
The scene switch uses the LabRuntime preparation and retirement path. The default
`Original Contract` keeps its original bundle and four camera profiles. The
extended selection adds these views under `Scene > Camera > Reference Views`:

| View | Runtime position | Runtime target | Vertical FOV, radians |
| --- | --- | --- | --- |
| `CAM_Clearcoat` | `(67, 3.2, -10)` | `(67, 0.8, 0)` | 0.6509917105 |
| `CAM_Anisotropy` | `(82.2, 4.4, -10)` | `(82.2, 0.8, 1.3)` | 0.6509917105 |

Positions map Blender `(X, Y, Z)` to runtime `(X, Z, Y)` in meters. Views retain
a 16:9 reference composition and near/far 0.1/100 m. Enabling the Physical Sun
preset uses 120000 lux and EV100 15 with profile version 2; the default reference
uses EV100 0 and profile version 1. Switching back to the original scene removes
the two views for absent stations.

Runtime tests cover IOR/clearcoat preservation, all six anisotropy material
inputs, imported mirror handedness, normal-image decoding and camera registration.
These CPU checks and the Content previews do not establish rendered direct/IBL
quality. GGLab's anisotropic IBL uses a bent reflection into an isotropic
prefiltered environment; matching DX12/Vulkan visual acceptance remains separate.

The installed derivative passed Content source/export/round-trip checks and the
180-check `app-content-registration` suite in both Debug and Release. The new
Lab selection and reference views have not been exercised through the native UI
in this handoff; no new Runtime captures were taken.
