# GGLab Lighting Contract

C0 reference content for World Lighting, loaded through the production asset and
Forward PBR paths. The asset contains 15 meshes, 9 authored materials, 3 exported
cameras and one orientation Empty. It has no textures or light objects. Keep
`GGLabLightingContract.gltf` and its adjacent `.bin` together.

## Source and export

Original project geometry and materials; no third-party content. Editable source:
`GraphicsGadgetLabContent/Scenes/GGLabLightingContract/GGLabLightingContract.blend`.
Content revision: `9fcc3132834cfd3de3234d89607583ea5ce84a9d`.
Exported with Blender 5.1.1 / glTF I/O 5.1.19 using the Content repository's
`Scripts/export_gltf.py`. Its scene README records the exact Blender geometry,
materials, cameras and authoring preview settings. This code change installs the
matching asset and Runtime reference profiles; it does not implement WL3 sun units.

| Artifact | SHA-256 |
| --- | --- |
| `GGLabLightingContract.blend` | `9f9a9633aed6f8d1d1293cb2f9757621cfd4df1b053eec617e31ea906ad04356` |
| `GGLabLightingContract.gltf` | `76ad082402bbca9e66eb51cd281b3f605ac308f2da3d735ed4b0d506049d7b7b` |
| `GGLabLightingContract.bin` | `6b0b356427f47eb62e41c3efdafe5380f06bc9a5a1d1602f56fa1f8d92b6aae3` |

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

`Scene > Camera > Reference Views` selects the three poses below; use
`Restore Reference View` after navigating. Each restoration also sets Manual
EV100 and compensation to zero and resets temporal history. The current viewport
aspect is preserved, with 16:9 as the authored composition reference.

| Reference | Runtime position | Runtime target | Vertical FOV (rad) |
| --- | --- | --- | --- |
| `CAM_ExposureChart` | `(0, 2, -10)` | `(0, 2, 0)` | 0.5483349022 |
| `CAM_LightingSphere` | `(16, 3.2, -9)` | `(16, 0.8, 0)` | 0.6509917105 |
| `CAM_SunAngles` | `(32, 6, -7)` | `(32, 1.4, 0)` | 0.6128792380 |

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

## Fixture interpretation

The chart's four 1.8 m cards have linear neutral RGB 0.02, 0.18, 0.50, 0.90 from
left to right, metallic 0 and perceptual roughness 1. Their Runtime normals are
`(0, 0, -1)`. The sphere view uses radius 0.8 m spheres with neutral RGB 0.18:
matte dielectric (roughness 1), rough dielectric (0.5), smooth dielectric (0.05),
and metal (metallic 1, roughness 0.1). There are no normal maps or emissive terms.

The orientation receivers share RGB 0.18, metallic 0 and roughness 1. Left to
right, their normals are `(0, 1, 0)`, `(0, 0, -1)` and
`(0, 1/sqrt(2), -1/sqrt(2))`. Their toward-light cosines are therefore
`sqrt(0.5)`, `sqrt(0.5)`, 1. The cube at `(21, 0.5, 3)` measures exactly one meter
on each side. Three 12 x 8 m ground tops sit at Runtime Y = 0, with RGB 0.08.

Rough PBR dielectrics include Fresnel-weighted diffuse and specular, so total
shaded color must not be compared directly to `rho * E / pi`. Tone mapping also
prevents one-stop exposure changes from becoming exact factors of two in SDR
pixels. Measure exposed-linear values for numeric claims. Physical sun, sky/IBL,
the full roughness sweep and aerial perspective remain later rendering work.

## Verification and manual handoff

`app-content-registration` verifies import through GGLab's actual importer,
texture absence, linear material factors, all fifteen mesh placements/dimensions,
card/receiver normals and restoration of all three camera profiles. Assimp may
merge equivalent meshes/materials; checks cover spatial triangles and numeric
material bindings rather than requiring authored mesh names/counts to survive. Project
ownership/filter checks and the Debug WinApp no-PCH build cover integration.

Manual DX12/Vulkan presentation and image validation are pending owner testing:

1. Launch each backend with the commands above and restore all three views.
   Confirm no missing assets, inverted receivers or clipped test targets.
2. Compare card ordering and sphere roughness; move the camera and restore it.
3. Sweep camera EV100 and toggle scene pre-exposure with TAA off. Compare at
   matching exposure and inspect the reported storage scale.
4. Switch to another Lab and back; confirm loading completes, cameras restore,
   environment state is restored on exit and validation reports no lifetime errors.

Blender authoring previews use different light/world/display settings and are not
Runtime golden images. No GPU presentation or visual-correctness claim is made
by the CPU checks.
