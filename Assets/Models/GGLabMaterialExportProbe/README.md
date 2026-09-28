# Material glTF import fixture

This small asset is for importer-contract validation. It is not a Runtime
image-quality scene or a material gallery. The nine one-material planes test
normal scale, occlusion strength, per-texture UV transforms, IOR, clearcoat,
anisotropy and sheen. Keep each `.gltf`, the shared `.bin` and the complete
`Textures/` directory together so either document resolves all external URIs.

`GGLabMaterialExportProbe.gltf` is the unchanged Blender 5.1.1 export.
`GGLabMaterialIOROnly.gltf` is a JSON-only derivative: on one material it
removes the specular extension that Blender needed to emit IOR 1.33, leaving
that material with only `KHR_materials_ior`. It shares the exported buffer and
images. The derivative is not evidence that Blender directly exports standalone
IOR. Neither document establishes what Assimp or the renderer currently
supports.

## Source and identity

All geometry, material values and tiny textures are original project work; no
third-party assets are included. The saved source and exporter/validator are in
`GraphicsGadgetLabContent/Scenes/GGLabMaterialExportProbe/` and its `Scripts/`
directory, at Content revision `9a7527d072ee00d4606b8bf098ca162e0ed163e1`.
The saved `.blend` SHA-256 is
`ea01ac90bf3d3d44afc8276a9ae7eded22d1cf9e0647d03ed7d6ed20dd694ad2`.

| Installed file | SHA-256 |
| --- | --- |
| `GGLabMaterialExportProbe.gltf` | `655279b78e3cfb6325ac05726bec37547f7e7c0c692fc2526ec0a3893ba61604` |
| `GGLabMaterialExportProbe.bin` | `b8b6233f6096a0137e47eb28fc6c4b096c99d97b7f4d01572c88bcb22118cbe1` |
| `GGLabMaterialIOROnly.gltf` | `4a746a9fd69d2f23a3ac5b3d05afd6027aa5d46491fac84f7b699408f766fad0` |
| `Textures/BaseColor.png` | `705b2917b1de0d52294971453f4d3b6f455c816d9ba3ffb9c70ec77150dd151f` |
| `Textures/CoatFactor-CoatRoughness.png` | `33d268e6a2a72eeeb061d51d034592ab4b39aaefd042ed04755038f3646f0850` |
| `Textures/CoatNormal.png` | `36555540203b2810de0141a6dada635e1e6d38bd9596e10e5fb152bb79760942` |
| `Textures/Image.png` | `865e64e7ce75d7d58d0d1f6f987cc2126327de4004e50282bf1eed249bdb6676` |
| `Textures/Normal.png` | `e80418f9a0d051e6af54e4069fa92520b973fcb88905fc024a95d7f6de16543e` |
| `Textures/Occlusion.png` | `ee518438a6c5f8d0e8eb9dc25001432ef5d2c14d90a6fe690e6967387471652e` |
| `Textures/SheenColor.png` | `19082b12f924f91f4d5ac13befd5caacca1caba3a547d4764edba9cc4c3223df` |
| `Textures/SheenRoughness.png` | `4c6b0ecb042830578cc09d8578ab4174c20594256b99b2e9948bf600dc28d428` |

## Reproduce

From the Content repository root, follow its probe README to export the saved
source and run `Scripts/validate_material_export_probe.py`. Create the second
document with `Scripts/make_material_ior_import_fixture.py`, passing the Blender
export as `--input` and `GGLabMaterialIOROnly.gltf` in the same output directory
as `--output`. Copy both `.gltf` files, the `.bin` and all exported textures to
this directory. Compare SHA-256 values before using the fixture in import tests.
