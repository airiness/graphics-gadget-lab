# GGLab Coastal Atrium

Original project greybox for the coastal courtyard, colonnade, stairs, corridor
and platform. The 86 authored meshes export as 1046 triangles and five opaque
untextured dielectric materials. Load the `.gltf` with its adjacent `.bin`.

## Source

- Repository: `GraphicsGadgetLabContent`.
- Saved source: `Scenes/GGLabCoastalAtrium/GGLabCoastalAtrium.blend`.
- Generator: `Scripts/create_coastal_atrium.py`.
- Exporter: `Scripts/export_gltf.py`, Blender 5.1.1 / glTF I/O 5.1.19.
- Source SHA-256: `34962d6fb0ed91c51c9e5d3632516aaa1ef93e46e8988f625fe414da8b0c9521`.
- glTF SHA-256: `54ad1232f56a512ff18c619d799a24dd16fb10c3652c550fa70f20ad3feb5e07`.
- Buffer SHA-256: `35fa68839620ab3ef412d0a61fed3a7ff790f55d1a6e21e11ab198ca704c79e9`.

The source was exported from the Content working tree; hashes identify the
delivery without requiring an unpublished commit. No third-party assets or
textures are used. The original island import fixture remains separate.

From the code repository root:

```powershell
& 'C:/Program Files/Blender Foundation/Blender 5.1/blender.exe' --background `
  --python-exit-code 1 --python ../GraphicsGadgetLabContent/Scripts/export_gltf.py -- `
  --input ../GraphicsGadgetLabContent/Scenes/GGLabCoastalAtrium/GGLabCoastalAtrium.blend `
  --output Assets/Models/GGLabCoastalAtrium/GGLabCoastalAtrium.gltf
```

Export reads the saved `.blend` without replacing it. Runtime content is loaded
when entering the Demo; automatic file watching is not provided. The Blender
exporter reports 240 unfreed blocks (0.109253 MB) at shutdown with exit code 0.

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

The default runtime view reproduces `CAM_Courtyard`. All three source cameras
have explicit runtime profiles described below. glTF cameras and Sun remain
reference data; the Demo configures its camera and light separately. The Blender
World and AgX previews are not runtime baselines. Interior darkness with
environment lighting disabled is expected at this stage.

## Reference camera profiles

In `Scene > Camera`, selecting a `Reference View` immediately restores it onto
Main Camera. `Restore Reference View` repeats that operation after navigation or
camera edits. Restoration selects Main for input and display, applies position,
orientation, vertical FOV, clip planes and 0 EV, clears movement velocity, and
requests a temporal reset through the existing camera-cut contract. It does not
create additional render views. Current viewport dimensions and aspect remain
in effect; the panel reports a mismatch with the intended 16:9 composition.

Profile 1 is provisional greybox composition, not Rendering Baseline 1. Runtime
definitions live in
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

The following values were read from the runtime `Camera` after restoration in
the headless content suite on 2026-09-16, using a 1920 by 1080 viewport (actual
float aspect `1.77777779`). Together with the positions and clip distances above,
they record the effective transform and perspective projection, independently of
the Blender lens labels. They are not measurements from the earlier window PNGs.

| Camera ID | Runtime yaw, radians | Runtime pitch, radians | Vertical FOV, degrees |
| --- | --- | --- | --- |
| `CAM_Courtyard` | -0.745419502 | -0.452466518 | 37.2990761 |
| `CAM_ShadowStairs` | -0.351444811 | -0.037537422 | 39.7607002 |
| `CAM_InteriorExterior` | 1.71968627 | -0.105563588 | 45.7473259 |

`Copy Camera Record` copies the selected camera's actual runtime position, basis,
yaw/pitch, projection, vertical FOV, aspect, clip planes and exposure. Its
"Last restored reference" label records provenance; subsequent edits are included
in the copied values. A stable camera ID alone does not establish an unchanged
profile. Composition changes require updating the profile version and recorded
parameters together.

Camera restoration leaves lighting and render feature edits in effect. The
startup sunlight, exposure and feature settings in the table above describe the
intended greybox comparison. Re-entering the Demo restores those startup settings.
The camera record is not a complete render capture manifest, and temporal reset
does not promise identical jitter, frame sequence or deterministic replay.

## Validation record

### Reference camera checks

Completed on 2026-09-16:

| Check | Result |
| --- | --- |
| Direct GGLabRuntime and GGLabRuntimeTests Debug x64 builds | Passed |
| WinApp Debug x64, `GGLAB_USE_PCH=0` and `GGLAB_USE_PCH=1` | Both passed |
| `diagnostics-contracts` | 81 checks passed |
| `rendering-contracts` | 307 checks passed |
| `app-content-registration`, both PCH modes | 32 checks passed in each build |
| Project boundaries / generated filter metadata | Passed |
| Source `.blend` and runtime `.gltf` / `.bin` hashes | Unchanged from the greybox delivery |

The camera checks cover invalid profile rejection, copied observations, stale
camera identities, Main input/display selection, residual velocity removal,
unchanged viewport aspect, and one temporal-reset request on every restoration.
Each atrium camera centers its authored target and restores identical view and
projection matrices after navigation and lens edits. These are CPU contract
checks; interactive controls, clipboard delivery, GPU output for the new views
and temporal accumulation after a camera cut are not established by these tests.

### Greybox import checks

Completed on 2026-09-15:

| Check | Result |
| --- | --- |
| WinApp Debug x64, `GGLAB_USE_PCH=0` | Passed |
| WinApp Debug x64, `GGLAB_USE_PCH=1` | Passed after renaming the comparison helper to avoid the Windows `near` macro |
| Project boundaries / filter metadata | Passed / unchanged |
| `app-content-registration` | 25 checks passed, including both real assets |
| `app-launch-options` / `app-host-configuration` | 26 / 11 checks passed |
| `app-lifecycle` | 11 checks passed |
| Headless executable launched from unrelated working directory | Passed |
| Saved Blender solids / export buffer and accessor ranges | Passed |
| Export source preservation | Saved `.blend` SHA-256 unchanged |
| Three Blender authoring previews | Rendered and inspected |

The import probes operate on transformed triangles rather than source mesh names:
they check the bounded footprint, floor heights, stair treads, empty openings,
solid wall faces on both sides, sill/lintel/roof enclosure, and thin pergola slats
separated by open gaps. These automated checks verify geometry and CPU import;
the static display references below provide separate visual evidence.

### Courtyard overview references

Static courtyard overview checks passed on Vulkan and DirectX 12 on 2026-09-16.
The captures show consistent model placement, door/window openings, stairs,
railings and major shadow positions, without obvious missing parts or axis errors.
The Demo selection and backend panel labels identify the content and renderer.

| Backend | Reference capture |
| --- | --- |
| Vulkan | [Courtyard overview](../../Media/GGLabCoastalAtrium/atrium-vulkan-overview.png) |
| DirectX 12 | [Courtyard overview](../../Media/GGLabCoastalAtrium/atrium-dx12-overview.png) |

Both original PNG files are preserved at 1922 by 1112, including window chrome
and DevTools. They are visual smoke references, not pixel-comparison golden images
or measured client extents. Fine surface patterns visible in both captures have
not been diagnosed by this static comparison. Runtime visual checks of the other two
candidate views, motion stability, detailed shadow quality, GPU validation logs,
Release builds and edited-export reload are outside this record.
