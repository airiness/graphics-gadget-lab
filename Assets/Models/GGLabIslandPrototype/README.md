# GGLab Island Prototype

This first-party primitive fixture validates the Blender-to-GGLab model path.
It contains seven meshes and four untextured metallic/roughness materials.
The editable source belongs to the separate `GraphicsGadgetLabContent`
repository; this directory contains the runtime glTF Separate export and its
external `.bin` buffer. Load the `.gltf`, keeping both files together.

## Source and export

- Content revision: `cad63a6`.
- Source: `Scenes/GGLabIslandPrototype/GGLabIslandPrototype.blend`.
- Source SHA-256: `8f5bd0b5c411d04b058247092823716cafc5e0f849bc5b558297f8d9853ae019`.
- glTF SHA-256: `b971bb89c2fc23f11156ff4ac67e97688e1c50df19b7cf7864d5c40f6e4c3c59`.
- Buffer SHA-256: `88bd7f695c7d78fab7972a9e1e02efbe0356efbcdb05ee5bf36697951a12db0e`.
- Exporter: `Scripts/export_gltf.py`, Blender 5.1.1, glTF I/O 5.1.19.
- These are original project primitives, with no third-party model or texture inputs.

Run from the code repository root, adapting the two sibling repository paths
and Blender installation if needed:

```powershell
& 'C:/Program Files/Blender Foundation/Blender 5.1/blender.exe' --background `
  --python ../GraphicsGadgetLabContent/Scripts/export_gltf.py -- `
  --output ./Assets/Models/GGLabIslandPrototype/GGLabIslandPrototype.gltf
```

The exporter reads the saved `.blend`; it does not regenerate or overwrite it.
Restart the island Demo after re-exporting. This workflow does not provide live
file watching.

## Runtime preset

```powershell
./Build/Output/x64/Debug/GraphicsGadgetLab.exe --demo island --rhi dx12 --absolute-mouse --no-devtools
./Build/Output/x64/Debug/GraphicsGadgetLab.exe --demo island --rhi vulkan --absolute-mouse --no-devtools
./Build/Output/x64/Debug/GraphicsGadgetLab.exe --self-test app-content-registration
```

The preset uses the existing Forward PBR pipeline and asynchronous Model Loading.
It creates one model entity at identity, without recentering or scaling. Assimp's
accumulated node transforms become mesh-instance local transforms; an editable
runtime node hierarchy is outside this fixture's scope.

| Setting | Value |
| --- | --- |
| Runtime coordinates | Left handed, Y up; Blender `(X, Y, Z)` becomes `(X, Z, Y)` |
| Units | One runtime unit per authored meter |
| Camera position / target | `(17, 16, -23)` / `(0, 0.8, 0)` |
| Vertical FOV / near / far | `0.4426289085` radians / `0.1` / `100` |
| Initial client extent | `1920 x 1080` |
| Main light ray direction | Normalized `(-0.6, -1.6, 0.4)` |
| Main light color / intensity | White / `3.0` runtime units; directional shadows enabled |
| Environment intensity / skybox | `0` / disabled; prior settings restored when leaving the preset |
| Exposure / tone mapping | `0 EV` / ACES fitted |
| TAA / GTAO / bloom | Disabled / disabled / disabled |

Camera and punctual light in the glTF remain reference data. The preset explicitly
sets their runtime equivalents; this does not validate camera/light import.
The Blender World and AgX view transform are not reproduced. Without environment
lighting, the metal can look dark away from the directional specular highlight.

| Material | Linear RGB | Metallic | Roughness |
| --- | --- | --- | --- |
| MAT_GreyRough | `(0.48, 0.48, 0.48)` | 0 | 0.8 |
| MAT_RedRough | `(0.65, 0.035, 0.025)` | 0 | 0.7 |
| MAT_Metallic | `(0.55, 0.58, 0.62)` | 1 | 0.23 |
| MAT_SmoothDielectric | `(0.015, 0.055, 0.105)` | 0 | 0.1 |

The desktop content self-test imports this exact fixture through `ModelImporter`.
It checks every authored world corner and its material assignment (including
rotated parent/child and nonuniform scale), 106 triangles, transformed normals
against triangle planes, absence of texture sources, and linear RGBA/M/R factors.
Assimp currently merges the seven source meshes into four instances; preserving
source mesh names/counts is not part of the import contract. Expected geometry and
materials are defined by this fixture.

## Screenshots

| Backend | Reference capture |
| --- | --- |
| Vulkan | [Window capture](../../Media/GGLabIslandPrototype/island-vulkan-baseline.png) |
| DirectX 12 | [Window capture](../../Media/GGLabIslandPrototype/island-dx12-baseline.png) |

The PNGs are 1922 by 1112, including window chrome and DevTools. They show the
runtime preset on each backend and are visual references, not pixel-comparison
golden images.
