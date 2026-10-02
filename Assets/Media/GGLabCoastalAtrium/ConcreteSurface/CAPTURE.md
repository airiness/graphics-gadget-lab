# Coastal Atrium Concrete Surface

SDR visual checks of the refined concrete material, saved on October 2, 2026
(UTC). The [asset reference](../../../Models/GGLabCoastalAtriumResearchLounge/README.md)
records the source/export identities and reproduction commands.

## Inputs

- Recorded code commit at capture: `99115fa410843c7e3dd26eac5a03cd3fd7822153`,
  with three concrete texture replacements and an
  additional import self-test in the working tree. Renderer, shader and camera
  sources are unchanged from that commit.
- Shader source tree: `1167f85ebf7fd2441e6e973415ed1dc502befea3`.
- Content source: the saved Coastal Atrium `.blend`, identified by SHA-256:
  `b0dbb35343489dcb04a753e2fd91b3789b4cd704f69e662248debb74be9d4882`.
- Blender 5.1.1 glTF Separate export: JSON, adjacent binary and twelve PNGs.
  All fourteen installed files match the independently regenerated Content
  export. Only the three concrete PNGs differ from the preceding metal export;
  geometry, UVs, tangents, hierarchy, materials, cameras and other PNGs are retained.
- WinApp Debug and Release x64 were built with Visual Studio 2022 Community's
  x64 MSBuild and `/p:PreferredToolArchitecture=x64`. Captures use Debug WinApp
  and ShaderCompiler binaries.
- GPU: NVIDIA GeForce RTX 5080. DX12 reported driver 32.0.16.1714. Vulkan
  reported driver 617.14, adapter API 1.4.351 and application baseline 1.3.

The recorded commit is historical context, not a required checkout target;
rebase or squash can rewrite it. The shader tree ID identifies content rather
than commit ancestry. Source/export file hashes in the asset reference and the
image/binary hashes below identify the actual capture inputs. These identities
remain valid through history changes only while their content is unchanged.

## Settings and capture method

| Setting | Value |
| --- | --- |
| Scene / transform | `Demo.Playground.CoastalAtrium` / identity |
| Output and internal resolution | 1920 x 1080 |
| Pipeline / lighting | Forward PBR / Forward+ |
| World Sun ray direction | Normalized `(-1, -0.85, 0.35)` |
| Sun TOA illuminance / angular radius | 120000 lux / 0.2666 degrees |
| Atmosphere | Enabled; Earth defaults |
| Sky source / skybox | Physical Sky / enabled |
| Published World Lighting | Active and requested generation 3; no retiring persistent textures |
| IBL | Calibrated intensity 1, world aligned; 512 x 512 x 6 FP32, ten mips |
| Exposure | Manual EV100 15, compensation 0 |
| TAA / GTAO / bloom | Disabled by the Atrium Demo profile |
| Other rendering settings | Current code defaults, including directional PCF shadows |

Both backends use isolated state roots:

```powershell
$captureStateRoot = Join-Path (Get-Location) 'Build/Verification/ConcreteImport'
./Build/Output/x64/Debug/GraphicsGadgetLab.exe --demo atrium --rhi dx12 `
  --state-root "$captureStateRoot/DX12State"
./Build/Output/x64/Debug/GraphicsGadgetLab.exe --demo atrium --rhi vulkan `
  --state-root "$captureStateRoot/VulkanState"
```

Use `Scene > World Lighting` to enable Physical Sun, Atmosphere, Physical Sky
and skybox. Wait for matching active/requested publication. Each view uses
`Scene > Camera > Reference Views`, profile version 1. Restoring a camera resets
its legacy exposure to zero; set Manual EV100 15 again after every restoration.
All views are perspective, roll-free, left-handed Y-up, with near/far 0.1/150 m
and aspect 16:9. Coordinates and FOV are unchanged from the authored profiles.

| View | Position, meters | Target, meters | Vertical FOV, degrees | DX12 | Vulkan |
| --- | --- | --- | --- | --- | --- |
| Courtyard | `(23, 19, -28)` | `(-1, 1.8, -2)` | 37.2990761 | [Image](courtyard-dx12.png) | [Image](courtyard-vulkan.png) |
| Shadow Stairs | `(5.5, 3.4, -14)` | `(0, 2.8, 1)` | 39.7607002 | [Image](shadow-stairs-dx12.png) | [Image](shadow-stairs-vulkan.png) |
| Interior / Exterior | `(-12, 4, -2.9)` | `(2, 2.5, -5)` | 45.7473259 | [Image](interior-exterior-dx12.png) | [Image](interior-exterior-vulkan.png) |
| Sky / Horizon | `(20, 5.5, -26)` | `(0, 3, 0)` | 45.7473259 | [Image](sky-horizon-dx12.png) | [Image](sky-horizon-vulkan.png) |

Allow model, texture and environment publication to finish. Close panels and
place the pointer in the title bar. Windows.Graphics.Capture returned a
1922 x 1112 window frame as JPEG. Decode it and crop the window rectangle
`(1, 100, 1920, 1010)` directly to PNG, without resizing or retouching. The crop
excludes the title/menu area, pointer and pointer highlight, plus the upper
part of the rendered viewport. Only the final cropped image is archived.
All eight saved PNGs were inspected for panels and pointers.

These are compressed SDR presentation references. They are neither raw GPU
readbacks nor pixel-comparison goldens; PNG encoding does not recover detail
lost in the source JPEG. File save times below are archival times, not GPU or
frame timestamps. Desktop HDR/color calibration was not verified.

## Verification and observations

- WinApp Debug and Release builds passed. `app-content-registration` passed
  206 checks in each configuration. The added concrete check covers UV0,
  metallic/roughness/normal factors, three semantic texture bindings,
  1024-square extent and eleven mip levels. Existing checks retain per-material
  triangles, orthonormal tangent frames, rock bindings and lounge extensions.
- Content validation passed against the installed bundle: palette, roughness,
  dielectric packing, unit normals, seams, packed source graph, preserved scene
  state and normalized orthogonal UV0 tangent frames on 21 concrete primitives.
  Export bytes match independent regeneration; glTF reimport retains color
  spaces, RGB/G/B connections and normal scale. Earlier contact probes apply
  to the unchanged geometry and were not rerun for this texture update.
- Both backends presented all four static views. Production import/upload
  reported 14 meshes, 19 instances and twelve textures; Assimp merges compatible
  authored meshes. Lit concrete retains warm broad variation and shallow fine
  relief, especially on the near wall in Interior / Exterior. Paired views show
  no obvious normal inversion, glossy concrete or backend-specific material loss.
- Both backends logged fence-dependent texture resource teardown. The resumed
  DX12 session and Vulkan exited with code 0. Interactive logs contained no
  error/critical entries. Vulkan's final diagnostic panel reported validation
  requested, messenger enabled, zero errors/warnings, zero invalid descriptor
  transitions/resource failures and healthy Runtime state.
- Startup retained the existing HDR FP16 sanitization warning: sixteen channels
  clamped to 65000. Physical Sky was the active capture environment. Debug import
  tests also retain the optional `KHR_materials_specular` core-fallback warning
  from their separate material fixture. Blender retained shared-image sampler
  warnings and its existing small shutdown allocation diagnostic (240 blocks,
  0.109253 MB). Export exited with code 0; installed sampler checks, repeated
  bytes and reimport passed.
- Metal rails/pergola retain visible PCF self-shadowing bands. The finite
  72 by 64 m ocean placeholder leaves a black below-horizon gap in the horizon
  and interior views, and an exposed background beyond the ocean in Courtyard.
  These are existing rendering/environment limitations.
- No continuous camera-path acceptance, temporal stability, hardware
  qualification or performance comparison is claimed. TAA remains disabled.

The frozen [earlier baseline](../Baseline1/CAPTURE.md), earlier Physical Sky
images and historical Atrium bundle are preserved. The preceding
[coastal detail captures](../CoastalDetails/CAPTURE.md) retain three matching
poses and lighting settings with the old concrete material. They provide static
material references; Interior / Exterior has no archived matching prior view.
These screenshots are not a renderer-only A/B comparison.

## Archived image identities

| Image | Saved UTC | SHA-256 |
| --- | --- | --- |
| [courtyard-dx12.png](courtyard-dx12.png) | 2026-10-02T14:04:06.997Z | `757239613771102e41c44d377d5b3bc3903580461861dab7f09d687fde313fd8` |
| [shadow-stairs-dx12.png](shadow-stairs-dx12.png) | 2026-10-02T16:11:56.363Z | `7b186814893b4ae479a2f9c63477ad80f95d000afc0dbcb2b58b182af36343b5` |
| [interior-exterior-dx12.png](interior-exterior-dx12.png) | 2026-10-02T16:13:29.197Z | `a2f9958d0f4d47b185b332b4e8c983f7f8aa46aecd644d1b2e458740cc31689e` |
| [sky-horizon-dx12.png](sky-horizon-dx12.png) | 2026-10-02T16:15:14.894Z | `1e812c542eb67f9eb3704804007bd37692a0d229fda9ce14350410c0fbe72bef` |
| [courtyard-vulkan.png](courtyard-vulkan.png) | 2026-10-02T16:19:43.082Z | `4688d79b161b31ae9ab627f8594c5ce1433481941b7bb88a8cef0000cc246669` |
| [shadow-stairs-vulkan.png](shadow-stairs-vulkan.png) | 2026-10-02T16:21:18.768Z | `7e345ce2bf4c346209fe6ddf942bd5b0d2b2fcfec176bd6ef4c6959288a3da88` |
| [interior-exterior-vulkan.png](interior-exterior-vulkan.png) | 2026-10-02T16:22:59.326Z | `cb3bb0a08b54933d945c75b89de0a394af93a03d7f96aac2c4505f7a12d109f2` |
| [sky-horizon-vulkan.png](sky-horizon-vulkan.png) | 2026-10-02T16:27:48.091Z | `0a722e2c22f48b0dae383f5160310daac493e7a9fbd5bdbfd6d84a8f9e2be06d` |

## Capture binary identities

| Binary | SHA-256 |
| --- | --- |
| Debug `GraphicsGadgetLab.exe` | `d8d3331c336c8ff711d6597838f49b2ff3f0bbad4780362e6da26b53350d332f` |
| Debug `gglab-shaderc.exe` | `d53ca7f3ed52bf1a940a1f999b6d42b45df6b2b51110659f45faa80be13f4256` |
| Debug `dxcompiler.dll` | `9a5100511e127c6a2fc78edf984f95074a76d35b90c90c4d342430a5ae160e9b` |
