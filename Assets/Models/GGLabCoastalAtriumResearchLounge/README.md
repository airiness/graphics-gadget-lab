# Coastal Atrium with Research Lounge

This original project asset combines the Coastal Atrium and coated Research
Lounge with detailed coastal shelves, dedicated rock textures, eight shared
boulder instances, static service equipment and refined concrete/metal edges.
The glTF Separate bundle contains 153 unique meshes, 166 nodes, eleven opaque
materials, four cameras, one reference Sun, one binary buffer and twelve PNGs.
There are 158 placed mesh objects, 24,658 placed triangles and 24,388 unique
triangles. Keep the `.gltf`, `.bin` and `Textures/` directory together.
No third-party assets are used.

## Source and identity

The editable source is
`GraphicsGadgetLabContent/Scenes/GGLabCoastalAtrium/GGLabCoastalAtrium.blend`.
The Content working tree is based on `0ded870` on `7-island-plan-procedural`,
with the subsequent metal refinement present as working-tree changes at export.
The saved source hash identifies that exact authored state; the base commit
alone does not reproduce this bundle. Blender 5.1.1 and Content's
`Scripts/export_gltf.py` exported the saved source without saving changes back.

| Artifact | SHA-256 |
| --- | --- |
| Source `.blend` | `ef1f456acd4780f356c8f2d30420d8981d9d04798c146b85d5e208eb619979e3` |
| `GGLabCoastalAtrium.gltf` | `18a2ca7bce8a701b7c33e64846f5347c618c83f3be468041120a5ff3d2371e2d` |
| `GGLabCoastalAtrium.bin` | `5fb21d820258097019dfabdc0b3247de4c98f836f178c08ae452641b95973c1a` |

## Authored content

- Coastal shelves contain 3470 triangles and retain the courtyard/platform
  foundation clearances. Eight boulders reuse three prototypes, adding 432
  placed triangles and 162 unique triangles.
- `MAT_CoastalRock` binds original Base Color, Normal and Metallic Roughness
  textures through UV0. Base Color is sRGB; Normal and packed G roughness /
  B metallic are linear data. Metallic is zero and normal scale is one.
- Service equipment has 41 static parts and 5188 triangles: cabinet, louver
  assembly, pipe and mounting hardware. `MAT_ServicePaint` is opaque ochre
  paint with roughness 0.46; `MAT_ServiceSeal` uses roughness 0.82.
- Seventeen concrete parts have baked bevels and 3006 triangles. Forty-nine
  railing/pergola parts have 1-3 mm bevels, base plates, end caps and joints,
  totaling 7888 triangles. Nineteen post/handrail joins have separate visible
  surface ownership. Object transforms and the four reference poses are retained.
- The 23-part lounge retains 4468 triangles. Its shell uses clearcoat factor
  0.82 and roughness 0.11. The brushed-aluminum frame retains metallic 1,
  roughness 0.28, anisotropy 0.78 and rotation 0; UV0 U follows each rail/leg.
  Cushions remain opaque. The repaired arm-shell surfaces are retained.
- The 72 by 64 m opaque ocean is a bounded foreground placeholder.

The existing concrete, paving and metal image bytes are unchanged. Three
coastal rock images are added. Detailed procedural recipes, contact checks and
pointer-free Blender previews live in the Content scene README.

## Reproduce and validate

Run from the Content repository root with the saved source identity above:

```powershell
$blender = 'C:/Program Files/Blender Foundation/Blender 5.1/blender.exe'
& $blender --background --factory-startup --python-exit-code 1 --python Scripts/export_gltf.py -- `
  --input Scenes/GGLabCoastalAtrium/GGLabCoastalAtrium.blend `
  --output ../GraphicsGadgetLab/Assets/Models/GGLabCoastalAtriumResearchLounge/GGLabCoastalAtrium.gltf
& $blender --background --factory-startup --python-exit-code 1 --python Scripts/validate_metal_structure.py -- `
  --source Scenes/GGLabCoastalAtrium/GGLabCoastalAtrium.blend `
  --reference Exports/Checks/MetalStructure/Before/GGLabCoastalAtrium.blend `
  --gltf ../GraphicsGadgetLab/Assets/Models/GGLabCoastalAtriumResearchLounge/GGLabCoastalAtrium.gltf `
  --compare Exports/Checks/MetalStructure/ExportB/GGLabCoastalAtrium.gltf
```

The Content scene README explains how to obtain the pre-metal source from
`0ded870` and generate the comparison export. Validation covers lounge surfaces,
terrain, rock textures, service parts, shared boulders, concrete contacts and
metal fittings. It includes 268 concrete and 255 metal surface probes,
UVs/tangent frames on refined parts, closed solids and glTF reimport. The
installed JSON, buffer and twelve images match the validated Content candidate
byte for byte. The saved source hash remained unchanged.

## Runtime entry

From the code repository root:

```powershell
./Build/Output/x64/Debug/GraphicsGadgetLab.exe --demo atrium --rhi dx12
./Build/Output/x64/Debug/GraphicsGadgetLab.exe --demo atrium --rhi vulkan
./Build/Output/x64/Debug/GraphicsGadgetLab.exe --self-test app-content-registration
```

`--demo atrium` loads this bundle through production asset preparation.
Use `WinApp` and `ShaderCompiler` outputs built from the same code revision.
Assimp merges compatible meshes; Runtime mesh/instance counts therefore differ
from the authored glTF inventory. Import checks count placed triangles by
material binding and decode every texture with its semantic format/mipmaps.
They also check geometry, imported tangent frames, dedicated rock bindings and
the existing shell clearcoat/frame anisotropy.

The four existing camera profiles are unchanged. Restore a reference camera,
then set Manual EV100 15 for physical World Sun / Physical Sky checks; camera
restoration retains the legacy zero-EV reference. The exported Sun and Blender
preview World do not define Runtime lighting units or IBL.

The earlier [Atrium asset](../GGLabCoastalAtrium/README.md), its frozen baseline
captures and the preceding Physical Sky captures retain their original
identities. The new [Content import captures](../../Media/GGLabCoastalAtrium/CoastalDetails/CAPTURE.md)
record matching DX12/Vulkan courtyard, stair and horizon views with Physical
Sky and EV100 15. Geometry changes prevent treating them as a renderer-only
comparison. WinApp Debug/Release builds and their `app-content-registration`
suites passed (205 checks each); the capture record states the remaining PCF
and bounded-ocean limitations.
