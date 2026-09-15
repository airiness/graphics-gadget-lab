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

The source includes `CAM_Courtyard`, `CAM_ShadowStairs` and
`CAM_InteriorExterior`. The default runtime view reproduces the courtyard camera;
the other two are provisional Blender composition references, not runtime camera
switches. glTF cameras and Sun remain reference data, and camera/light import is
not claimed. The Blender World and AgX previews are not runtime baselines. Interior
darkness with environment lighting disabled is expected at this stage.

## Validation record

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
not been diagnosed by this static comparison. Runtime checks of the other two
candidate views, motion stability, detailed shadow quality, GPU validation logs,
Release builds and edited-export reload are outside this record.
