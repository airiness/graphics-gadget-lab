# Coastal Retreat Runtime bundle

The Coastal Atrium Demo (`--demo atrium`, persisted id
`Demo.Playground.CoastalAtrium`) loads this original project scene. It replaces
its Research Lounge presentation with pale mineral architecture, timber screens
and decking, supported furniture and planters, coastal vegetation, an opaque
sea surface and three distant landforms. Keep the `.gltf`, adjacent `.bin` and
`Textures/` directory together. No third-party assets are used.

## Installed bundle identity

All Runtime inputs for this scene are tracked under
`Assets/Models/GGLabCoastalRetreat` in this repository. Builds, loading and
verification use the installed glTF Separate bundle directly. No asset
generation step or additional asset repository is required. The SHA-256 values
below identify the shipped files independently of authoring history.

The bundle has 446 unique glTF meshes, 694 placed mesh nodes, 719 total nodes,
318,952 unique / 1,867,078 placed triangles, nineteen opaque materials, nine
reference cameras and one reference Sun. Twenty-four original 1024-square PNGs
form eight PBR map sets: Retreat lime, stone, timber, sea and leaf, plus the
retained project rock, metal and upholstery maps. Base Color uses sRGB; Normal
and packed G roughness / B metallic use linear data through UV0. Material
factors, texture transforms and instance transforms remain as exported.

| Installed file | SHA-256 |
| --- | --- |
| `GGLabCoastalRetreat.bin` | `be15e043f5625079112bc881dccd69cdd135df687fc7eaff709202a0efb8bb64` |
| `GGLabCoastalRetreat.gltf` | `4aff8d929c4fd887f3b921a9f439ac0a8fa70c2b82a18abfba511fcc493ac1b4` |
| `Textures/CoastalRock_BaseColor.png` | `56c9d606ac6fa3bf7bcbf50a159660d93b349bc435a454bf811f6cc83cab62f9` |
| `Textures/CoastalRock_MetallicRoughness.png` | `4d9a8ab323d8cd305186df980a96471eb04eb482858bf43d79b8c4c580804342` |
| `Textures/CoastalRock_Normal.png` | `d2e0a7a84ca0761d316acb0a5297a9412d857a76907e69963fb38302f887d9f8` |
| `Textures/Metal_BaseColor.png` | `cc09b179567bac43a80d988a64d9089b7c00ea6aea5da00db1ac38b337936064` |
| `Textures/Metal_MetallicRoughness.png` | `fc86a16fe6f9bf0613593de00471673d0c625514934cea89a4d4c52f42024a1f` |
| `Textures/Metal_Normal.png` | `b027121d763390e6687f134c74b7ceaac0da4ab92a750536e582e841d15bb476` |
| `Textures/RetreatLeaf_BaseColor.png` | `484c83e4560db452521bfb92b7220063f7a153d5c6feb1be19285dda2fae949b` |
| `Textures/RetreatLeaf_MetallicRoughness.png` | `cf93da884bbc4864e42e6bf6df3ef440c01c07a52e824aee89f9881e56c7aca3` |
| `Textures/RetreatLeaf_Normal.png` | `42d35cbcad7f809413b345b6f2334f72df778fa71f65a522aef051b9a886853e` |
| `Textures/RetreatLime_BaseColor.png` | `4e6167e58c2b2f5ab4eaf612afefe4be4475f930510d566b37bc1d7f2b1e69e2` |
| `Textures/RetreatLime_MetallicRoughness.png` | `a4934b5f01d57b0541716a5e307279e69490abdd9cafc94da3bef832d1bc8ecd` |
| `Textures/RetreatLime_Normal.png` | `973d7b917704d3356e1bdc69e77b6ab8a807a1ac332f47c2197bb118c6c836ae` |
| `Textures/RetreatSea_BaseColor.png` | `f8eb6ed70d054cdbb0245e65abf3b8738b62ef0aced314ecf2a3dd300d66a191` |
| `Textures/RetreatSea_MetallicRoughness.png` | `bc3c844c0b33306104b24dc37ad20e5bd085864dd08a855d676d4dd042b79190` |
| `Textures/RetreatSea_Normal.png` | `d26dac42ed564f639b784cc8150778eb3b910c9a08b8365736dc8c73669fb053` |
| `Textures/RetreatStone_BaseColor.png` | `3e8b0c8b17638b8ac0b1399abd8e72cb1e20add0783c22e63d6aefb7c6096524` |
| `Textures/RetreatStone_MetallicRoughness.png` | `746aec17aa5ff97b9b419b01eeecb8b0dbe55cb29dbe084076308c1d177dee9f` |
| `Textures/RetreatStone_Normal.png` | `f495f98af96dfcc887a5eab0cc7d87c01621c792ec5adbc0057148961c3ac6f4` |
| `Textures/RetreatTimber_BaseColor.png` | `ccac841ba3b20c3978317f6866ccd38ceb78ff04f835d4b7fe0934d71f09f07b` |
| `Textures/RetreatTimber_MetallicRoughness.png` | `727737ccecffdd08de569a4473cd6145556b0d388e2c749c4e52afcdc0f71635` |
| `Textures/RetreatTimber_Normal.png` | `fe75dc3c10bb4d42ae5aa8e998446e85795da8b750e1fd1d7db31771b25c345d` |
| `Textures/Upholstery_BaseColor.png` | `8b609f23151ddc922cd926183f7aeb1b3e099ac0b8177d7553a6de943fa54f08` |
| `Textures/Upholstery_MetallicRoughness.png` | `15ea1ce5c4fc92f21e5588929b044b903ec137fbf15e44abcdb995a571ff1964` |
| `Textures/Upholstery_Normal.png` | `5f433933356f2de09dcf550a2cc1e1267ce74287d5b7a89795b08a73dcf4b3d6` |

## Runtime views and verification

The Demo starts at `Retreat_Overview`. Five new reference views register the
authored positions, targets and exported vertical FOV in runtime coordinates,
mapping Blender `(X, Y, Z)` to `(X, Z, Y)` in meters. They use a 0.05 m near plane,
6000 m far plane and reference aspect 16:10. Camera restoration
keeps the actual viewport aspect. The eight preceding Atrium reference views
remain available with their original poses and projections.

| View | Purpose |
| --- | --- |
| `Retreat_Hero` | Whole island, sea and distant landforms |
| `Retreat_Courtyard` | Window planting, timber screening and colonnade |
| `Retreat_Lounge` | Lounge, deck and supported table props |
| `Retreat_Planting` | Foliage, soil and planter seating detail |
| `Retreat_Overview` | Both terraces, courtyard circulation and stair approach |

The production import suite checks placed triangles per material, opaque
bindings, finite geometry, orthonormal tangent frames and all twenty-four
textures' semantic decoding and mip chains. Build WinApp and ShaderCompiler
from the same code revision, then run from the code repository root:

```powershell
Build/Output/x64/Debug/GraphicsGadgetLab.exe --self-test app-content-registration
powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/GGLabSession.ps1 start -Session retreat-dx12 -Rhi dx12 -Demo atrium -WindowSize 1280x800
powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/GGLabSession.ps1 batch -Session retreat-dx12 -Views Retreat_Hero,Retreat_Courtyard,Retreat_Lounge,Retreat_Planting,Retreat_Overview -SettleFrames 16
powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/GGLabSession.ps1 stop -Session retreat-dx12
```

Repeat with `-Rhi vulkan` and a separate session id. Sessions are hidden by
default. See [Frame Capture](../../../Docs/FrameCapture.md) for sidecars and
cross-backend comparisons. Import checks and shader compilation alone do not
establish visual correctness.

Local verification on 2026-10-06: Debug WinApp and ShaderCompiler built
successfully; `app-content-registration` passed all 280 checks using the installed
assets in this repository. Hidden DX12 and Vulkan sessions each captured all five
Retreat views at 1280 x 800 after sixteen settled frames. All ten images and
readiness sidecars were inspected. Geometry, foliage,
furniture, texture bindings and distant coast were present on both backends.
The five paired captures had mean absolute RGB differences of 0.0419 to 0.1270
on the 8-bit scale; at most 0.0061% of pixels exceeded a channel difference of 8.
Camera and capture settings agreed; total simulation time differed by four
seconds in this static scene. No assertion, validation error or upload failure
was reported. The existing HDR FP16 sanitization warning occurred on both runs.
Release and performance/LOD qualification were not run.

## Presentation limits

GGLab uses the Demo's existing runtime lighting and post-processing. Blender
World lighting, Cycles, AgX, depth of field and Hero's off-centre lens shift are
not glTF rendering contracts; the runtime Hero camera uses a symmetric
projection. The Demo's existing environment override disables IBL and the
skybox, leaving a black background and dark unlit shadows in these captures.
The sea is static opaque PBR geometry. Vegetation is static
solid geometry; this import adds no plant LOD, wind animation or performance
budget guarantee. Existing Atrium/Research Lounge bundles and their frozen
capture baselines retain their bytes.
