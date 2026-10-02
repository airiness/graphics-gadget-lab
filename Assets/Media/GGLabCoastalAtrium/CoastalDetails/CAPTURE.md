# Coastal Atrium Content Import

SDR visual checks of the refined coast, rock material, service equipment,
boulders and architectural/metal edges, saved on October 2, 2026 (UTC).
The [asset reference](../../../Models/GGLabCoastalAtriumResearchLounge/README.md)
describes the current bundle and Runtime verification workflow. This record retains
the earlier capture's source identity below, before concrete material refinement.

## Inputs

- Recorded code commit at capture: `6ee509d8c069f2c5f580ed973e495aa98c45fb9c`,
  with this asset replacement and expanded import
  self-tests in the working tree. Renderer, shader and camera sources are
  unchanged from that commit.
- Shader source tree: `1167f85ebf7fd2441e6e973415ed1dc502befea3`.
- Authoring-source fingerprint (provenance only; source not distributed here),
  with refined metal geometry and the original concrete maps:
  `ef1f456acd4780f356c8f2d30420d8981d9d04798c146b85d5e208eb619979e3`.
- Blender 5.1.1 glTF Separate export: JSON, adjacent binary and twelve PNGs.
  The installed bundle matches the independently validated authoring candidate.
- WinApp Debug x64 and ShaderCompiler Debug were built with Visual Studio 2022
  Community's x64 MSBuild and `/p:PreferredToolArchitecture=x64`.
- GPU: NVIDIA GeForce RTX 5080. The Vulkan panel reported driver 617.14,
  adapter API 1.4.351 and application baseline 1.3, with validation requested.

The recorded commit is historical context, not a required checkout target;
rebase or squash can rewrite it. The shader tree ID identifies content rather
than commit ancestry. Use the source SHA-256 above and image/binary hashes below
to identify these capture inputs. File hashes remain valid while their bytes
are unchanged. Compared with the handoff recorded in
[Concrete Surface](../ConcreteSurface/CAPTURE.md), JSON, binary and nine other
PNGs are identical; the original export and concrete map hashes are recorded here.

| Earlier export artifact | SHA-256 |
| --- | --- |
| `GGLabCoastalAtrium.gltf` | `18a2ca7bce8a701b7c33e64846f5347c618c83f3be468041120a5ff3d2371e2d` |
| `GGLabCoastalAtrium.bin` | `5fb21d820258097019dfabdc0b3247de4c98f836f178c08ae452641b95973c1a` |
| `Concrete_BaseColor.png` | `e818a7ae00626effb5fcc87b52b9e9671cfb60555d5ea6630a2d6ae79e65410d` |
| `Concrete_Normal.png` | `3cfc07edbd853b63a489d4badb4852891447779c51012bede6362c2db83dcb89` |
| `Concrete_MetallicRoughness.png` | `4c86c67ae2978d6d92b7b2c5e95f4ac8ba8d70fa9131dd1e8bf1a4095493a2e9` |

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
| Sky / Horizon | `(20, 5.5, -26)` | `(0, 3, 0)` | 45.7473259 | [Image](sky-horizon-dx12.png) | [Image](sky-horizon-vulkan.png) |

DX12 was launched through the desktop helper, then switched from the Start Demo
to Atrium through Demo Selection. Vulkan used a fresh state directory:

```powershell
$captureStateRoot = Join-Path (Get-Location) 'Build/Verification/AtriumImport/VulkanState'
./Build/Output/x64/Debug/GraphicsGadgetLab.exe --demo atrium --rhi vulkan `
  --state-root $captureStateRoot
```

Allow model, texture and environment publication to finish. Close panels and
place the pointer in the title bar. Windows.Graphics.Capture returned a
1922 x 1112 window frame as JPEG. Decode it and crop the window rectangle
`(1, 100, 1920, 1010)` directly to PNG, without resizing or retouching. The crop
excludes the title/menu area, pointer and pointer highlight, plus the upper
part of the rendered viewport. Only the final cropped image is archived.
All six saved PNGs were inspected for panels and pointers.

These are compressed SDR presentation references. They are neither raw GPU
readbacks nor pixel-comparison goldens; PNG encoding does not recover detail
lost in the source JPEG. File save times below are archival times, not GPU or
frame timestamps. Desktop HDR/color calibration was not verified.

## Verification and observations

- WinApp Debug and Release builds passed. `app-content-registration` passed
  205 checks in each configuration: texture decoding/semantics, valid geometry,
  orthonormal imported tangent frames, per-material placed triangles, rock UV0
  bindings and retained lounge clearcoat/anisotropy.
- Authoring validation passed against the installed bundle: save/source identity,
  repeated export bytes, reimport, lounge surface separation, terrain support,
  service/boulder clearances, 268 concrete and 255 metal contact/detail probes.
- Both backends presented the three views with the new coast, rock maps,
  equipment, boulders and fittings. Production import/upload reported 14 meshes,
  19 instances and twelve textures; Assimp merges compatible authored meshes.
- Vulkan exited with code 0 and logged fence-dependent texture resource teardown.
  Its session log had no error/critical entries or validation warnings/errors.
  Startup retained the existing HDR FP16 sanitization warning: sixteen channels
  clamped to 65000. Physical Sky was the active capture environment.
- Metal rails/pergola retain visible PCF self-shadowing bands. The finite
  72 by 64 m ocean placeholder does not cover the distant horizon; a black
  below-horizon band is visible in the horizon view. These remain recorded
  rendering/environment limitations.
- No Interior / Exterior capture, continuous camera-path acceptance, temporal
  stability, hardware qualification or performance comparison is claimed.

The frozen [earlier baseline](../Baseline1/CAPTURE.md), earlier Physical Sky
images and historical Atrium bundle are preserved. Changed geometry/material
inputs prevent treating these new images as a renderer-only A/B comparison.

## Archived image identities

| Image | Saved UTC | SHA-256 |
| --- | --- | --- |
| [courtyard-dx12.png](courtyard-dx12.png) | 2026-10-02T12:22:28.852Z | `e9475ff274ab7f7aa5748f3ab0de9b79a5f8f19b8f49b2e21236afc4c560713d` |
| [shadow-stairs-dx12.png](shadow-stairs-dx12.png) | 2026-10-02T12:25:56.147Z | `8477456d84e7d15694834559d9491402e21db138f0259c2f1c0939d8a8a9675c` |
| [sky-horizon-dx12.png](sky-horizon-dx12.png) | 2026-10-02T12:33:00.088Z | `b51702740ea7d19ffd39bcd889989f37e039792638bfa46203c1b02746c96c1e` |
| [courtyard-vulkan.png](courtyard-vulkan.png) | 2026-10-02T12:40:06.828Z | `ee909b43b48cae187367308b4e14d4afce29f30ad08e878c1604d647e54dee82` |
| [shadow-stairs-vulkan.png](shadow-stairs-vulkan.png) | 2026-10-02T12:43:02.751Z | `83cdcf11480a2eb711113f69f6bfa590b93b93199f49463bfab4ae57cafd5116` |
| [sky-horizon-vulkan.png](sky-horizon-vulkan.png) | 2026-10-02T12:46:17.323Z | `a83875bf48be9e2bb9e78d69c24442c8e7fa218c0076d37566269d505e19ecf4` |

## Capture binary identities

| Binary | SHA-256 |
| --- | --- |
| Debug `GraphicsGadgetLab.exe` | `a9e320f2b51071ca8519d55db1b25d5a24c8fd18c0e332b0d3a8a0d08fba9f68` |
| Debug `gglab-shaderc.exe` | `d53ca7f3ed52bf1a940a1f999b6d42b45df6b2b51110659f45faa80be13f4256` |
| Debug `dxcompiler.dll` | `9a5100511e127c6a2fc78edf984f95074a76d35b90c90c4d342430a5ae160e9b` |
