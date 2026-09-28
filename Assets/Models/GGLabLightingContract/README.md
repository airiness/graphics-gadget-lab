# GGLab Lighting Contract

C0 reference content for World Lighting, loaded through the production asset and
Forward PBR paths. The asset contains 28 meshes, 21 authored materials, 4 exported
cameras and one orientation Empty. It has no textures or light objects. Keep
`GGLabLightingContract.gltf` and its adjacent `.bin` together.

## Source and export

Original project geometry and materials; no third-party content. Editable source:
`GraphicsGadgetLabContent/Scenes/GGLabLightingContract/GGLabLightingContract.blend`.
Content working-tree extension based on revision
`9fcc3132834cfd3de3234d89607583ea5ce84a9d`. The hashes below identify the exact
uncommitted source and installed export paired with the new Runtime profile.
Exported with Blender 5.1.1 / glTF I/O 5.1.19 using the Content repository's
`Scripts/export_gltf.py`. Its scene README records the exact Blender geometry,
materials, cameras and authoring preview settings. This code change installs the
matching asset and Runtime reference profiles.

| Artifact | SHA-256 |
| --- | --- |
| `GGLabLightingContract.blend` | `841bdbc7c7b7590d0aae269c61084778a73e6a26404fa32a336b9f913100f498` |
| `GGLabLightingContract.gltf` | `602805e187a5197e1878e1403a883afafbc259ddf760166ed7328cfa889643b4` |
| `GGLabLightingContract.bin` | `9ec30b41a3775e8f4f94d465dfe90105b3e0bed227fb423547c1085f774f2ecf` |

To re-export, run from the Content repository root:

```powershell
$blender = 'C:/Program Files/Blender Foundation/Blender 5.1/blender.exe'
& $blender --background --factory-startup --python-exit-code 1 --python Scripts/export_gltf.py -- `
  --input Scenes/GGLabLightingContract/GGLabLightingContract.blend `
  --output ../GraphicsGadgetLab/Assets/Models/GGLabLightingContract/GGLabLightingContract.gltf
```

## Run and inspect

Run from the code repository root:

```powershell
./Build/Output/x64/Debug/GraphicsGadgetLab.exe --lab gglab.lab.lighting_contract --rhi dx12 --absolute-mouse
./Build/Output/x64/Debug/GraphicsGadgetLab.exe --lab gglab.lab.lighting_contract --rhi vulkan --absolute-mouse
./Build/Output/x64/Debug/GraphicsGadgetLab.exe --self-test app-content-registration
```

Select `Lighting Contract` in `Lab Control > Active Lab` under `Demo.LabHost`.
LabRuntime prepares the model asynchronously, commits switches through its safe
command path and retains retired session assets until their last-use GPU fence.
Only the active session changes environment intensity/skybox visibility, restoring
the previous values on exit. Pending or cancelled loads do not change them.

`Lab Control > Reference Views` selects the four poses below; use
`Restore Reference View` after navigating. Each restoration also sets Manual
EV100 to 0 in legacy mode or 15 in Physical Sun mode, clears exposure compensation,
and resets temporal history. The current viewport
aspect is preserved, with 16:9 as the authored composition reference.

| Reference | Runtime position | Runtime target | Vertical FOV (rad) |
| --- | --- | --- | --- |
| `CAM_ExposureChart` | `(0, 2, -10)` | `(0, 2, 0)` | 0.5483349022 |
| `CAM_LightingSphere` | `(16, 3.2, -9)` | `(16, 0.8, 0)` | 0.6509917105 |
| `CAM_SunAngles` | `(32, 6, -7)` | `(32, 1.4, 0)` | 0.6128792380 |
| `CAM_RoughnessSweep` | `(48, 3.5, -13)` | `(48, 2, 0)` | 0.6509917105 |

All views use near/far = 0.1 / 100 m. Startup uses `CAM_ExposureChart`.
Blender `(X, Y, Z)` maps to Runtime `(X, Z, Y)` in meters. Mesh nodes retain their
imported transforms beneath an identity model entity; the camera conversion is
not reapplied to the geometry.

## Initial rendering preset

- One white directional light, ray direction `normalize(0, -1, 1)`, intensity 3
  in **legacy renderer units**, without shadows. This is not a calibrated sun or
  a lux value. The shading vector toward the light is `normalize(0, 1, -1)`.
- TAA, GTAO and Bloom disabled in the Lab profile. Scene pre-exposure defaults off.
- Environment intensity zero and skybox disabled while the Lab is active.
- Manual EV100 = 0, compensation = 0: exposure scale = `1/1.2`, pre-exposure = 1.
- Normal production PBR and FinalColor tone mapping; no custom fixture shader.

Use `Scene > Camera` to change Manual EV100/compensation. The Post Process
inspector's `Override Scene Pre-exposure` / `Enable Scene Pre-exposure` controls
exercise storage scaling with TAA off. Keep global diagnostic overrides in mind
when comparing the default preset. Reference-view restoration resets camera
exposure but does not reset those overrides.

`Physical Sun (120 klux / EV15)` changes the same light to the physical World Sun,
sets Manual EV100 15 for every reference view and enables scene pre-exposure.
For physical sky/IBL, enable Atmosphere, select Physical Sky and enable the skybox
in `Scene > World Lighting`. Inspect the published source/generation before
comparing views. Blender lighting and previews do not define these Runtime values.

## Fixture interpretation

The chart's four 1.8 m cards have linear neutral RGB 0.02, 0.18, 0.50, 0.90 from
left to right, metallic 0 and perceptual roughness 1. Their Runtime normals are
`(0, 0, -1)`. The sphere view uses radius 0.8 m spheres with neutral RGB 0.18:
matte dielectric (roughness 1), rough dielectric (0.5), smooth dielectric (0.05),
and metal (metallic 1, roughness 0.1). There are no normal maps or emissive terms.

Roughness Sweep adds two matched rows at X = 42.5, 44.7, 46.9, 49.1, 51.3, 53.5,
Runtime Z = 0. Lower dielectric centers have Y = 0.8; upper metallic centers have
Y = 3.2. Both rows use linear RGB 0.18 and authored perceptual roughness
**0, 0.05, 0.1, 0.25, 0.5, 1**, left to right. Every sphere has radius 0.8 m,
64 segments, 32 rings, 3968 triangles and smooth outward normals. The new ground
is 16 by 8 m with RGB 0.08. The original stations and reference profiles are preserved.

The production BRDF clamps perceptual roughness to **0.045** before squaring it
to GGX alpha (`Shaders/PBR/BRDF.hlsli`). Imported zero remains zero and exercises
this Runtime floor. Finite-solar-disk and IBL quality limits remain renderer-owned;
these inputs and SDR observations do not establish a numeric approximation error bound.

The orientation receivers share RGB 0.18, metallic 0 and roughness 1. Left to
right, their normals are `(0, 1, 0)`, `(0, 0, -1)` and
`(0, 1/sqrt(2), -1/sqrt(2))`. Their toward-light cosines are therefore
`sqrt(0.5)`, `sqrt(0.5)`, 1. The cube at `(21, 0.5, 3)` measures exactly one meter
on each side. Three 12 x 8 m ground tops sit at Runtime Y = 0, with RGB 0.08.

Rough PBR dielectrics include Fresnel-weighted diffuse and specular, so total
shaded color must not be compared directly to `rho * E / pi`. Tone mapping also
prevents one-stop exposure changes from becoming exact factors of two in SDR
pixels. Measure exposed-linear values for numeric claims. This fixture supplies
no distant aerial-perspective geometry.

## Verification and manual handoff

`app-content-registration` verifies import through GGLab's actual importer,
texture absence, linear material factors, all twenty-eight mesh placements/dimensions,
card/receiver normals, smooth sweep normals and restoration of all four camera
profiles. Assimp may merge equivalent meshes/materials; checks cover spatial
triangles and numeric material bindings rather than requiring authored mesh
names/counts to survive. Project filter metadata was checked without changes to
source ownership.

For manual DX12/Vulkan presentation and image inspection:

1. Launch each backend with the commands above and restore all four views.
   Confirm no missing assets, inverted receivers or clipped test targets.
2. Compare card ordering and sphere roughness; move the camera and restore it.
3. Sweep camera EV100 and toggle scene pre-exposure with TAA off. Compare at
   matching exposure and inspect the reported storage scale.
4. Switch to another Lab and back; confirm loading completes, cameras restore,
   environment state is restored on exit and validation reports no lifetime errors.

Blender authoring previews use different light/world/display settings and are not
Runtime golden images. No GPU presentation or visual-correctness claim is made
by the CPU checks.

Content validation covers saved-source preservation, material factors, sphere
radius/tessellation/normals, camera framing and Blender round-trip. Repeated
exports are byte-identical; original mesh payloads, material factors and camera
nodes match the previous installed asset. Runtime import and GPU checks remain
separate verification gates.

The installed extension passed Debug and Release WinApp builds and
`app-content-registration` in each configuration (120 checks). Debug DX12 and
Vulkan presentation was inspected at 16:9 in `CAM_RoughnessSweep`, first with the
legacy preset and then with Physical Sun at 120000 lux / EV100 15, Earth
Atmosphere, Physical Sky, environment intensity 1 and yaw 0. All twelve spheres
were visible, with the expected roughness progression in both rows. Diagnostics
confirmed matching active/requested lighting generations. Switching to Coastal
Atrium and normal shutdown completed without assertion, shader or GPU lifetime
errors; Vulkan diagnostics reported zero validation errors/warnings. The existing
startup HDR texture sanitization warning remains. These SDR inspections are not
pixel golden tests or quantitative BRDF/IBL accuracy measurements.

## Physical Sky screenshots

[Capture settings and identities](../../Media/WorldLighting/PhysicalSkyReferences/CAPTURE.md)
record the paired, cursor-free SDR captures of `CAM_RoughnessSweep`:
[DirectX 12](../../Media/GGLabLightingContract/PhysicalSky/roughness-sweep-dx12.png)
and [Vulkan](../../Media/GGLabLightingContract/PhysicalSky/roughness-sweep-vulkan.png).
The 1920 by 1060 crops exclude window chrome and the top menu without resampling.
