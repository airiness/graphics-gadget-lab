# GGLab Texture Contract

Original diagnostic content for the rendering playground's material input checks.
The board has 18 meshes, 36 triangles, 15 exported materials and five PNG images.
Keep the `.gltf`, adjacent `.bin` and `Textures/` together.

## Source and export

The editable source is `GraphicsGadgetLabContent/Scenes/GGLabTextureContract/GGLabTextureContract.blend`.
`Scripts/create_texture_contract.py` generates the scene and exact diagnostic
pixels; `Scripts/export_gltf.py` exports the saved source with UV0 and tangents.
All geometry and texture data are original project work, with no third-party assets.
Exported with Blender 5.1.1 / glTF I/O 5.1.19.

| Artifact | SHA-256 |
| --- | --- |
| Saved `.blend` | `78503a8f05bcad67a153777fa13f335993bea1e48e90752e836ae30fd0b594c3` |
| `.gltf` | `d0b64ff5db17eb8231cc7b06c8af4ffb1ab938e911967e9514631a3df0507de6` |
| `.bin` | `8936806ef89ac8d2c0d3e7e2c90f89b2598e89029d29b3151779a37e84f1f809` |

```powershell
./Build/Output/x64/Debug/GraphicsGadgetLab.exe --demo texture-contract --rhi dx12 --absolute-mouse
./Build/Output/x64/Debug/GraphicsGadgetLab.exe --demo texture-contract --rhi vulkan --absolute-mouse
./Build/Output/x64/Debug/GraphicsGadgetLab.exe --self-test app-content-registration
```

`Demo.Playground.TextureContract` uses the normal Playground asset preparation,
material upload and Forward PBR path. Its camera is `(0, 2.3, -12.5)`, looking at
`(0, 2.3, 0)`, with vertical FOV 0.6509917105 radians, near/far 0.1/50 m and
reference aspect 16:9. `Scene > Camera > Reference Views` restores this view.
The white directional light has intensity 3 and normalized ray direction
`(-0.45, -0.65, 1)`. Shadows, environment lighting, skybox, TAA, GTAO and bloom are
disabled for this diagnostic preset; exposure is 0 EV. Atrium shadow settings are
independent and unchanged.

## Board layout

Each row has four columns, ordered from left to right in the reference view.

| Row | Column 1 | Column 2 | Column 3 | Column 4 |
| --- | --- | --- | --- | --- |
| Top | Direction markers, 1 tile | Same image, 2 by 2 tiles | sRGB 128 image times linear factor 0.5 | Linear base-color factor 0.10793025 |
| Middle | Four tangent-space normal directions | Same normal map with mirrored U | Four independent vertex-normal references | Flat surface normal |
| Bottom | Raw MR channels, factors 1 | Same MR channels, factors 0.5 | Four independent material-factor references | Same G/B with inverted unused R |

The direction texture has red/green upper quadrants and blue/yellow lower
quadrants, a white right arrow and cyan up arrow. Image rows start at the top.
The normal texture's quadrants tilt toward tangent +X, -X, +Y and -Y. Mirrored U
changes both sampling and tangent handedness. The reference normals use the
normalized decoded RGB vectors; small differences from 8-bit quantization and
the current shader's reconstructed Z are possible.

The MR image stores roughness 64/255 on the left and 192/255 on the right,
metallic 0 on top and 1 on the bottom. Its R checkerboard is deliberately unused;
no occlusion texture is bound. The scaled and reference materials use roughness
0.1254902/0.3764706 and metallic 0/0.5. The inverted R image must not introduce a
checkerboard. Perspective-dependent specular response can vary across board
positions, so these are input and directional comparisons, not pixel-identical
lit-image patches.

Base-color RGB uses sRGB decoding before multiplication by its linear factor.
Normal and MR images are linear; MR uses G for roughness and B for metallic.
These follow the [glTF material conventions](https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html#materials).
Images are 256 square except the constant 8 square gray patch. Runtime mipmaps
and repeat samplers use the existing asset pipeline.

## Verification

The 2026-09-17 CPU checks cover actual PNG decoding, semantic view formats and
mipmaps, adjacent relative paths, known pixel bytes, factors, UV0 orientation,
2 by 2 repetition, mirrored tangents and the absence of implicit occlusion.
All exported accessor/index ranges and tangent bases passed structural checks;
exported PNGs are byte-identical to the source images.

These probes exposed and now guard against an Assimp conversion issue: authored
glTF bitangents received an extra sign flip in the left-handed conversion.
`ModelImporter` compensates before post-processing, while generated tangents
remain unchanged. The `asset-data` suite covers regular and mirrored UVs for
both authored and generated tangents.

Blender's authoring preview was inspected separately. CPU import checks do not
establish GPU sampling, material upload or presentation correctness. DX12/Vulkan
visual acceptance for this board and the textured atrium is not recorded yet.
