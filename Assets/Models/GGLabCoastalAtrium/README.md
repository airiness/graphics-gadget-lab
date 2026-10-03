# GGLab Coastal Atrium

Original project coastal courtyard, colonnade, stairs, corridor and platform.
The asset contains 86 authored meshes, 1046 triangles and five opaque materials.
Basic concrete, stone and metal surfaces use nine original procedural PNGs.
Load the `.gltf` with its adjacent `.bin` and `Textures/` directory.

This bundle retains the earlier content reference. The current `--demo atrium`
loads [the Research Lounge bundle](../GGLabCoastalAtriumResearchLounge/README.md).
Existing captures below retain their original export identity. Use the recorded
exported files for those comparisons; the current Demo bundle also includes
the coated lounge, brushed frame and subsequent coastal/material refinements.

## Source

- Exporter: Blender 5.1.1 / glTF I/O 5.1.19.
- Authoring-source SHA-256 (provenance): `709cf13a6ef7b827736b7978c949608659624b519b882dc2519ae7920a9cbfaf`.
- glTF SHA-256: `7bcfc2ecf7e4f48446128d73af936d5147dafd8756d83ed56ce24ce892c14192`.
- Buffer SHA-256: `b31b36f65c0eb2aaca8ca280aa50ebe2446138bcf0ef4c957c4e4bcffbe52e7e`.

No third-party assets are used. The island import fixture is a separate asset.

This export adds the `CAM_SkyHorizon` reference to the earlier scene.
Geometry buffer, nine image files,
materials, transforms, original cameras and the reference Sun are preserved.

The [bundle contract](../README.md) describes installed-content validation.
Runtime content is loaded when entering the Demo; automatic file watching is
not provided. Historical comparisons use their frozen bundle, not a re-export.

## Basic materials

| Material | Surfaces | Textures | Metallic / roughness |
| --- | --- | --- | --- |
| `MAT_Concrete` | Walls, columns and roofs | 512 square concrete set | 0 / approximately 0.76-0.88 |
| `MAT_Paving` | Courtyard, stairs and platform | 512 square stone set | 0 / approximately 0.63-0.88 |
| `MAT_Structure` | Pergola and railings | 256 square brushed metal set | 1 / approximately 0.31-0.41 |
| `MAT_CoastalRock` | Terrain shelves | Concrete set with darker linear tint | 0 / concrete roughness |
| `MAT_OceanPlaceholder` | Bounded sea plane | Untextured | 0 / 0.15 |

Each set contains sRGB base color, linear +Y tangent normal and linear packed
metallic/roughness (G roughness, B metallic). All normal scales are 1; no separate
occlusion texture is bound. UV0 uses face projection at 2 meters per repeat;
paving has staggered 0.5 meter tiles. Textures supply detail without adding
geometry. N-gons are triangulated only in the transient export scene so Blender
can emit complete tangent data.

The [texture contract board](../GGLabTextureContract/README.md) supplies independent
channel/factor and normal-direction comparisons for this authoring path.

## Runtime preset

```powershell
./Build/Output/x64/Debug/GraphicsGadgetLab.exe --demo atrium --rhi dx12 --absolute-mouse
./Build/Output/x64/Debug/GraphicsGadgetLab.exe --demo atrium --rhi vulkan --absolute-mouse
./Build/Output/x64/Debug/GraphicsGadgetLab.exe --self-test app-content-registration
```

`Demo.Playground.CoastalAtrium` uses the existing Playground preparation lifecycle,
Forward PBR pipeline and one model entity at identity. One authored meter maps to
one runtime unit, with Blender `(X, Y, Z)` mapped to runtime `(X, Z, Y)`.

| Setting | Value |
| --- | --- |
| Courtyard / platform height | 2.40 / 0.90 m |
| Courtyard / platform size | 18 by 14 / 22 by 5 m |
| Stairs | Ten 0.15 m risers; 0.35 m treads; 5 m wide |
| Corridor wall / clear height | 0.55 / 4.20 m |
| Bounded ocean footprint | 72 by 64 m, height -0.05 m |
| Camera position / target | `(23, 19, -28)` / `(-1, 1.8, -2)` |
| Vertical FOV / near / far | 0.6509917105 radians / 0.1 / 150 m |
| Reference aspect ratio | 16:9 |
| Main light ray direction | Normalized `(-1, -0.85, 0.35)` |
| Light color / intensity | White / 3.0 runtime units; directional shadows enabled |
| Environment / skybox | Intensity 0 / disabled; restored on leaving the preset |
| Exposure / tone mapping | 0 EV / ACES fitted |
| TAA / GTAO / bloom | Disabled / disabled / disabled |

The default runtime view reproduces `CAM_Courtyard`. All four source cameras
have explicit runtime profiles described below. glTF cameras and Sun remain
reference data; the Demo configures its camera and light separately. The Blender
World and AgX previews are not runtime baselines. Interior darkness with
environment lighting disabled is expected.

## Reference camera profiles

In `Scene > Camera`, selecting a `Reference View` immediately restores it onto
Main Camera. `Restore Reference View` repeats that operation after navigation or
camera edits. Restoration selects Main for input and display, applies position,
orientation, vertical FOV, clip planes and 0 EV, clears movement velocity, and
requests a temporal reset through the existing camera-cut contract. It does not
create additional render views. Current viewport dimensions and aspect remain
in effect; the panel reports a mismatch with the intended 16:9 composition.

The reference views use profile version 1. Runtime definitions live in
[`CoastalAtriumReferenceViews.h`](../../../Sources/WinApp/Application/Demo/CoastalAtriumReferenceViews.h).
The coordinate system is left-handed, Y-up, in meters. Each perspective camera
uses near/far distances of 0.1/150 m and derives roll-free orientation from its
position and target. Blender's horizontal sensor/lens values are converted to
vertical FOV at the reference aspect, independently of window size.

| Camera ID | Runtime position | Look-at target | Observation purpose |
| --- | --- | --- | --- |
| `CAM_Courtyard` | `(23, 19, -28)` | `(-1, 1.8, -2)` | Courtyard scale, connected levels and primary lighting; initial hero view |
| `CAM_ShadowStairs` | `(5.5, 3.4, -14)` | `(0, 2.8, 1)` | Near railings and stair contacts, mid-distance slat shadows and distant columns |
| `CAM_InteriorExterior` | `(-12, 4, -2.9)` | `(2, 2.5, -5)` | Thick doorway occlusion and the corridor-to-courtyard brightness transition |
| `CAM_SkyHorizon` | `(20, 5.5, -26)` | `(0, 3, 0)` | Broad sky region above architecture, coastal platform and shoreline; vertical FOV 0.7984415392 radians |

The runtime camera angles and vertical field of view for each profile are:

| Camera ID | Runtime yaw, radians | Runtime pitch, radians | Vertical FOV, degrees |
| --- | --- | --- | --- |
| `CAM_Courtyard` | -0.745419502 | -0.452466518 | 37.2990761 |
| `CAM_ShadowStairs` | -0.351444811 | -0.037537422 | 39.7607002 |
| `CAM_InteriorExterior` | 1.71968627 | -0.105563588 | 45.7473259 |
| `CAM_SkyHorizon` | -0.655695626 | -0.076066793 | 45.7473259 |

`Copy Camera Record` copies the selected camera's actual runtime position, basis,
yaw/pitch, projection, vertical FOV, aspect, clip planes and exposure. Its
"Last restored reference" label identifies the source profile; subsequent edits
are included in the copied values. Composition changes require updating the
profile version and parameters together.

Camera restoration leaves lighting and render feature edits in effect. The
startup sunlight, exposure and feature settings in the table above describe the
runtime preset. Re-entering the Demo restores those startup settings.
The camera record is not a complete render capture manifest, and temporal reset
does not promise identical jitter, frame sequence or deterministic replay.

For physical sky/IBL inspection in `CAM_SkyHorizon`, designate the existing
Runtime directional light as World Sun, set physical illuminance (for example
120000 lux), enable Atmosphere, select Physical Sky and enable the skybox. Set
Manual EV100 15 after restoring the view: the unchanged profile policy restores
legacy 0 EV. The exported reference Sun and Blender World do not configure
physical lighting. The bounded ocean remains an opaque foreground placeholder.

Debug DX12 and Vulkan presentation was inspected at 16:9 after restoring
`CAM_SkyHorizon`, with the existing Sun direction, physical illuminance 120000
lux, Manual EV100 15, Earth Atmosphere, Physical Sky, environment intensity 1 and
yaw 0. The camera pose, sky region, architecture and shoreline composition
matched across backends. TAA, GTAO and Bloom remained disabled; scene
pre-exposure remained at the Demo default. Active/requested lighting generations
matched, and switching from Lighting Contract and normal shutdown completed
without assertion, shader or GPU lifetime errors. This inspection does not add
frozen captures to Rendering Baseline 1 or establish numeric lighting accuracy.

## Known limitations

Current directional shadows can show wave patterns from PCF self-shadowing.
Environment lighting is disabled in this preset, so interior surfaces and metal
away from direct highlights can appear dark.

With Physical Sky enabled, `CAM_SkyHorizon` exposes a black band between the sky
horizon and the far edge of the finite ocean plane on both DX12 and Vulkan.
The new camera preserves this visible limitation; the ocean remains a local
placeholder and does not extend to the physical horizon. The camera addition
does not change sky/ground rendering or supply distant atmospheric geometry.

## Screenshots

[Physical Sky capture settings](../../Media/WorldLighting/PhysicalSkyReferences/CAPTURE.md)
record `CAM_SkyHorizon` at EV100 15:
[DirectX 12](../../Media/GGLabCoastalAtrium/PhysicalSky/sky-horizon-dx12.png)
and [Vulkan](../../Media/GGLabCoastalAtrium/PhysicalSky/sky-horizon-vulkan.png).
These cursor-free 1920 by 1060 crops preserve the known horizon black band and
exclude window chrome and the top menu without resampling.

[Rendering Baseline 1](../../Media/GGLabCoastalAtrium/Baseline1/CAPTURE.md)
provides frozen asset and renderer references, capture settings, three paired
DX12/Vulkan views, and archived diagnostic panels.

### Basic materials

| Reference view | DirectX 12 | Vulkan |
| --- | --- | --- |
| Courtyard | [Material overview](../../Media/GGLabCoastalAtrium/atrium-dx12-materials-courtyard.png) | [Material overview](../../Media/GGLabCoastalAtrium/atrium-vulkan-materials-courtyard.png) |
| Shadow Stairs | [Paving, railings and slat shadows](../../Media/GGLabCoastalAtrium/atrium-dx12-materials-shadow-stairs.png) | [Paving, railings and slat shadows](../../Media/GGLabCoastalAtrium/atrium-vulkan-materials-shadow-stairs.png) |
| Interior / Exterior | [Corridor and courtyard transition](../../Media/GGLabCoastalAtrium/atrium-dx12-materials-interior-exterior.png) | [Corridor and courtyard transition](../../Media/GGLabCoastalAtrium/atrium-vulkan-materials-interior-exterior.png) |

### Greybox

| Backend | Reference capture |
| --- | --- |
| Vulkan | [Courtyard overview](../../Media/GGLabCoastalAtrium/atrium-vulkan-overview.png) |
| DirectX 12 | [Courtyard overview](../../Media/GGLabCoastalAtrium/atrium-dx12-overview.png) |

The basic-material and greybox PNGs are 1922 by 1112, including window chrome
and DevTools. They show the scene on each backend and are visual references,
not pixel-comparison golden images.
