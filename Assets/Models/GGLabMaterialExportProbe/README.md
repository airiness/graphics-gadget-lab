# Material glTF import fixture

This small asset is for importer-contract validation. It is not a Runtime
image-quality scene or a material gallery. The seven one-material planes test
normal scale, occlusion strength, per-texture UV transforms, IOR, clearcoat,
and anisotropy. Keep each `.gltf`, the shared `.bin` and the complete
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
directory. The probe was revised on 2026-09-30 to remove the deferred fiber
response, from Content baseline `df187bb`; the source hash identifies this generation.
The saved `.blend` SHA-256 is
`a3d08657745f90ff4186c844d1196d32348b78928bd4bceb7f1cdbb09fb44dc7`.

| Installed file | SHA-256 |
| --- | --- |
| `GGLabMaterialExportProbe.gltf` | `fd933d0944e4cbd1188c449cb052de6c0ca78f051e5f5d5794f849a8118bb7f6` |
| `GGLabMaterialExportProbe.bin` | `a46a8b888b9c6232ed15bdc23e8baee92fd43672ef8fd588dabbbc4b1eb055fd` |
| `GGLabMaterialIOROnly.gltf` | `b7e7e1f5272cecf6e457f0ac7a24b1fb6f704e9b370aa560e2a018597d865379` |
| `Textures/BaseColor.png` | `705b2917b1de0d52294971453f4d3b6f455c816d9ba3ffb9c70ec77150dd151f` |
| `Textures/CoatFactor-CoatRoughness.png` | `33d268e6a2a72eeeb061d51d034592ab4b39aaefd042ed04755038f3646f0850` |
| `Textures/CoatNormal.png` | `36555540203b2810de0141a6dada635e1e6d38bd9596e10e5fb152bb79760942` |
| `Textures/Image.png` | `865e64e7ce75d7d58d0d1f6f987cc2126327de4004e50282bf1eed249bdb6676` |
| `Textures/Normal.png` | `e80418f9a0d051e6af54e4069fa92520b973fcb88905fc024a95d7f6de16543e` |
| `Textures/Occlusion.png` | `ee518438a6c5f8d0e8eb9dc25001432ef5d2c14d90a6fe690e6967387471652e` |

## Reproduce

From the Content repository root, follow its probe README to export the saved
source and run `Scripts/validate_material_export_probe.py`. Create the second
document with `Scripts/make_material_ior_import_fixture.py`, passing the Blender
export as `--input` and `GGLabMaterialIOROnly.gltf` in the same output directory
as `--output`. Copy both `.gltf` files, the `.bin` and all exported textures to
this directory. Compare SHA-256 values before using the fixture in import tests.
