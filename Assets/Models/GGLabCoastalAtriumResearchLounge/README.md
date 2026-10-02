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

The bundle includes the refined coast, architectural/metal edges and concrete
material. The file SHA-256 values below identify the installed assets
independently of branch names or Git commit history. Rebase can change commit
identifiers; file hashes remain valid while the file bytes are unchanged.
Blender 5.1.1 exported the saved authoring source without changing it. Its source
fingerprint is retained as provenance only; the editable source is not shipped here.

| Artifact | SHA-256 |
| --- | --- |
| Authoring-source fingerprint (provenance) | `b0dbb35343489dcb04a753e2fd91b3789b4cd704f69e662248debb74be9d4882` |
| `GGLabCoastalAtrium.gltf` | `18a2ca7bce8a701b7c33e64846f5347c618c83f3be468041120a5ff3d2371e2d` |
| `GGLabCoastalAtrium.bin` | `5fb21d820258097019dfabdc0b3247de4c98f836f178c08ae452641b95973c1a` |
| `Textures/Concrete_BaseColor.png` | `a72bce9124eafc7f4f3153afc8284a868f0f3eb1f11f8c6bd1857e588edb9a4c` |
| `Textures/Concrete_Normal.png` | `6b9f1617caac4ef09e2e3a26ce585afe8e3d22e23e193efd750ce4e5de9ed0c1` |
| `Textures/Concrete_MetallicRoughness.png` | `9ac7b9afe3777e152a2b97839924f3431fc9309089be82fad04dd55529a4bb18` |

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
- `MAT_Concrete` uses three original 1024-square RGB8 maps: sRGB warm cement
  with aggregate and sparse pores, linear +Y tangent normals from shallow
  relief, and linear G roughness / B metallic zero. Roughness is 0.72-0.93
  before quantization. UV0 repeats over a 2 m tile; base/roughness/normal factors
  are one. There is no occlusion input, displacement or baked illumination.
- The 23-part lounge retains 4468 triangles. Its shell uses clearcoat factor
  0.82 and roughness 0.11. The brushed-aluminum frame retains metallic 1,
  roughness 0.28, anisotropy 0.78 and rotation 0; UV0 U follows each rail/leg.
  Cushions remain opaque. The repaired arm-shell surfaces are retained.
- The 72 by 64 m opaque ocean is a bounded foreground placeholder.

Compared with the preceding bundle recorded in the coastal detail captures,
only the three concrete PNGs change. The glTF JSON, binary geometry and nine
other PNGs are byte-identical.
JSON and buffer hashes alone therefore cannot identify this material update.

## Installed bundle validation

Use the committed glTF, buffer and twelve PNGs as the Runtime inputs. Compare
their file hashes and run `app-content-registration` as shown below. The
[bundle contract](../README.md) describes the public validation workflow.

Authoring validation against this installed bundle covered concrete palette,
roughness, dielectric packing, unit normals, periodic seams, source graph and
21 glTF tangent frames. It verifies all objects, UVs, corner normals, hierarchy,
other materials/images and scene settings are preserved. Reimport verifies
RGB/G/B connections, color spaces and normal scale. All fourteen installed files
match the independently regenerated export byte for byte; only three
PNGs differ from the prior export. The saved source hash remained unchanged.
Earlier lounge, terrain, equipment and 268 concrete / 255 metal contact probes
remain applicable to the unchanged geometry; those probes were not rerun for
this texture update.

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
the existing shell clearcoat/frame anisotropy. Concrete checks verify UV0,
retained factors, texture semantics and three 1024-square images with eleven
mip levels each.

The four existing camera profiles are unchanged. Restore a reference camera,
then set Manual EV100 15 for physical World Sun / Physical Sky checks; camera
restoration retains the legacy zero-EV reference. The exported Sun and Blender
preview World do not define Runtime lighting units or IBL.

The earlier [Atrium asset](../GGLabCoastalAtrium/README.md), its frozen baseline
captures and the preceding Physical Sky captures retain their original
identities. The preceding [coastal detail captures](../../Media/GGLabCoastalAtrium/CoastalDetails/CAPTURE.md)
retain the pre-concrete material. New [concrete surface captures](../../Media/GGLabCoastalAtrium/ConcreteSurface/CAPTURE.md)
record matching DX12/Vulkan courtyard, stair, interior/exterior and horizon views
with Physical Sky and EV100 15. WinApp Debug/Release builds and their
`app-content-registration` suites passed (206 checks each). The capture record
states the remaining PCF and bounded-ocean limitations.
