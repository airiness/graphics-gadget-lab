# Coastal Atrium with Research Lounge

This original project asset extends the Coastal Atrium with a two-place coated
Research Lounge and a brushed-aluminum frame beneath the pergola. The glTF
Separate bundle contains 109 meshes, 115 nodes, nine opaque materials, four
cameras, one Sun reference, one binary buffer and nine PNG textures. Keep the
`.gltf`, `.bin` and `Textures/` directory together. No third-party assets are used.

## Source and identity

The editable source is
`GraphicsGadgetLabContent/Scenes/GGLabCoastalAtrium/GGLabCoastalAtrium.blend`
with the source identity recorded below. Blender 5.1.1 and
`GraphicsGadgetLabContent/Scripts/export_gltf.py` produced this bundle, including
the lounge-side and courtyard-support surface correction.

| Artifact | SHA-256 |
| --- | --- |
| Source `.blend` | `8e59998427ff09c0cf34df1209e6787304163cd96b41ba627987790c58aa9dc1` |
| `GGLabCoastalAtrium.gltf` | `c6e32f9f86e9c77981d2a77382391fc391df41eee013db283f4ed080c020f8a5` |
| `GGLabCoastalAtrium.bin` | `e925e714da8dca494aa9c82a76ddaf6479a10bfe797f866ef41bcedea14e5c29` |

The shell retains clearcoat factor 0.82 and roughness 0.11. Its eight frame
parts use `MAT_LoungeBrushedAluminum`: linear RGB `(0.52, 0.56, 0.59)`, metallic
1, base roughness 0.28, anisotropy strength 0.78 and rotation 0. UV0 U follows
the length of each rail or leg. End bevels turn with the surface; the frame has
no normal map or mirrored UV. Cushions use opaque base materials. All nine PNGs,
material factors, transforms and reference cameras are unchanged.

Each arm shell is trimmed by its back wing to remove duplicate visible side
patches. The upper terrain shelf is trimmed around the courtyard slab; the slab
owns the exposed front surface above Z = 1.90 m in Blender coordinates. Only
these three meshes change, with closed source solids and retained outside
normals/UVs. Stairs and both platform heights remain unchanged. The bundle has
5530 triangles, including 4468 in the 23-part lounge.

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
UV/tangent direction, geometry, cameras, closed source solids and round-trip
import. Source and imported meshes are checked for duplicate visible surfaces
on both lounge sides and the courtyard support. Repeated exports produced
identical JSON, buffer and image bytes.

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
