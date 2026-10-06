# Coastal Atrium with Research Lounge

This retained bundle supports historical captures and import checks. The current
`--demo atrium` loads [Coastal Retreat](../GGLabCoastalRetreat/README.md).

This original project asset combines the Coastal Atrium and coated Research
Lounge with detailed coastal shelves, dedicated rock textures, eight shared
boulder instances, static service equipment, refined concrete/metal edges,
stone paving, satin structure metal and woven cushions.
The glTF Separate bundle contains 153 unique meshes, 166 nodes, eleven opaque
materials, four cameras, one reference Sun, one binary buffer and fifteen PNGs.
There are 158 placed mesh objects, 24,658 placed triangles and 24,388 unique
triangles. Keep the `.gltf`, `.bin` and `Textures/` directory together.
No third-party assets are used.

## Source and identity

The bundle includes the refined coast, architectural/metal edges, concrete,
stone paving, shared metal and upholstery materials. The file SHA-256 values
below identify the installed assets independently of branch names or Git commit
history. Rebase can change commit
identifiers; file hashes remain valid while the file bytes are unchanged.
Blender 5.1.1 exported the saved authoring source without changing it. Its source
fingerprint is retained as provenance only; the editable source is not shipped here.

| Artifact | SHA-256 |
| --- | --- |
| Authoring-source fingerprint (provenance) | `bd2f21b56a43075a5ac4a5c1902b3bccc48dbd3d7dc01bd561b75c8c4ee00678` |
| `GGLabCoastalAtrium.gltf` | `2bddb863ef030d7bf56822b301eb8d0dc584167260ad1208ebeef4b7a4a29863` |
| `GGLabCoastalAtrium.bin` | `741fc6668d7ee5bbc55a1cc80dc61dd3b90de8b316bde5e0e88604dbd89f0f1b` |
| `Textures/Concrete_BaseColor.png` | `a72bce9124eafc7f4f3153afc8284a868f0f3eb1f11f8c6bd1857e588edb9a4c` |
| `Textures/Concrete_Normal.png` | `6b9f1617caac4ef09e2e3a26ce585afe8e3d22e23e193efd750ce4e5de9ed0c1` |
| `Textures/Concrete_MetallicRoughness.png` | `9ac7b9afe3777e152a2b97839924f3431fc9309089be82fad04dd55529a4bb18` |
| `Textures/Stone_BaseColor.png` | `9ba6d6555520f77bb07fcb519730eb6363fc18b5e2553a2b9d69679dbd2c2fb8` |
| `Textures/Stone_Normal.png` | `b517b49532f0da891df1da937adaa76028f02835c14ea19a568601b5d739d962` |
| `Textures/Stone_MetallicRoughness.png` | `3f4bad3b871c0390c15893af2596a7e95f934817bb2ec048905d47a1241f33f0` |
| `Textures/Metal_BaseColor.png` | `cc09b179567bac43a80d988a64d9089b7c00ea6aea5da00db1ac38b337936064` |
| `Textures/Metal_Normal.png` | `b027121d763390e6687f134c74b7ceaac0da4ab92a750536e582e841d15bb476` |
| `Textures/Metal_MetallicRoughness.png` | `fc86a16fe6f9bf0613593de00471673d0c625514934cea89a4d4c52f42024a1f` |
| `Textures/Upholstery_BaseColor.png` | `8b609f23151ddc922cd926183f7aeb1b3e099ac0b8177d7553a6de943fa54f08` |
| `Textures/Upholstery_Normal.png` | `5f433933356f2de09dcf550a2cc1e1267ce74287d5b7a89795b08a73dcf4b3d6` |
| `Textures/Upholstery_MetallicRoughness.png` | `15ea1ce5c4fc92f21e5588929b044b903ec137fbf15e44abcdb995a571ff1964` |

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
- `MAT_Paving` uses three 1024-square maps with cool-grey block variation,
  mineral flecks, shallow mortar joints and localized edge wear. Its existing
  half-meter staggered layout and 2 m UV0 tile are retained; roughness is
  0.55-0.90 before quantization, with metallic zero.
- `MAT_Structure` uses three 1024-square satin-metal maps on 70 railing,
  pergola and service-metal parts. Assorted shallow finishing marks retain an
  isotropic response, 2 m UV0 repeat, metallic one and roughness 0.28-0.50.
- `MAT_LoungeUpholstery` uses three 1024-square opaque dielectric maps with
  approximately 2 mm warp/weft spacing, subtle neutral albedo and roughness
  0.80-0.96. Only the four cushion UV0 projections change, to a 0.25 m local
  repeat; geometry, topology, normals, placement and material bindings remain
  intact. Its base/roughness/normal factors are one and metallic factor is zero.
- The 23-part lounge retains 4468 triangles. Its shell uses clearcoat factor
  0.82 and roughness 0.11. The brushed-aluminum frame retains metallic 1,
  roughness 0.28, anisotropy 0.78 and rotation 0; UV0 U follows each rail/leg.
  Cushions remain opaque. The repaired arm-shell surfaces are retained.
- The 72 by 64 m opaque ocean is a bounded foreground placeholder.

Compared with the preceding concrete bundle, six Stone/Metal PNGs change and
three Upholstery PNGs are added. Six Concrete/CoastalRock PNGs are byte-identical.
The JSON and buffer change for the cushion material and four UV/tangent streams;
all positions, normals, other mesh streams, material bindings, camera/light poses
and placed triangle counts are retained. File identity includes textures as well
as the glTF and buffer.

## Installed bundle validation

Use the installed glTF, buffer and fifteen PNGs as the Runtime inputs. Compare
their file hashes and run `app-content-registration` as shown below. The
[bundle contract](../README.md) describes the public validation workflow.

Authoring validation covered independent generation, map palettes/roughness,
metallic packing, unit normals, seams, packed source graphs and glTF tangent
frames. Preservation checks cover all content outside the targeted graphs/maps
and four cushion UVs. Reimport retains live RGB/G/B connections, color spaces,
normal scale and quarter-meter cushion UVs through glTF's V conversion. All
seventeen installed files match the independently regenerated export byte for
byte. The saved source hash remained unchanged. Earlier lounge, terrain,
equipment and 268 concrete / 255 metal contact probes apply to unchanged
geometry; those historical probes were not rerun for this material update.

WinApp Debug/Release x64 builds and their `app-content-registration` suites
passed with 220 checks each. The added surface checks verify all three map
bindings, repeating UV0, white base/roughness/normal factors, intended metallic
factors, isotropic response and eleven mips per 1024-square texture. All eight
Runtime camera views pass target/projection and repeat-restoration checks.
Debug DX12 production startup resolved fifteen textures, completed their GPU
uploads and activated the Atrium with fourteen imported meshes and nineteen
instances. Six [DX12 surface captures](../../Media/GGLabCoastalAtrium/SurfaceMaterials/CAPTURE.md)
record Physical Sun/Sky at EV100 15. Matching Vulkan presentation and camera-motion
checks remain pending.

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

The four established camera profiles are unchanged. Four additional Runtime
views inspect paving, railing metal, the cushions and their weave; they add no
camera nodes to the glTF. Restore a reference camera,
then set Manual EV100 15 for physical World Sun / Physical Sky checks; camera
restoration retains the legacy zero-EV reference. The exported Sun and Blender
preview World do not define Runtime lighting units or IBL.

The [manual capture guide](../../Media/GGLabCoastalAtrium/SurfaceMaterials/CAPTURE_GUIDE.md)
lists six recommended views per backend, matching lighting settings and raw
window-image handling. Six DX12 scene crops and their raw/cropped file identities
are archived in the capture record. Vulkan screenshot review and continuous
camera-motion acceptance for this bundle remain pending.

The earlier [Atrium asset](../GGLabCoastalAtrium/README.md), its frozen baseline
captures and the preceding Physical Sky captures retain their original
identities. The preceding [coastal detail captures](../../Media/GGLabCoastalAtrium/CoastalDetails/CAPTURE.md)
retain the pre-concrete material. New [concrete surface captures](../../Media/GGLabCoastalAtrium/ConcreteSurface/CAPTURE.md)
record matching DX12/Vulkan courtyard, stair, interior/exterior and horizon views
with Physical Sky and EV100 15 for the preceding bundle. That historical capture
record retains its own verification, file identities and PCF/bounded-ocean
limitations. These images predate the current paving, metal and upholstery maps.
