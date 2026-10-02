# Physical Sky reference captures

Four manual SDR captures of the installed roughness-sweep and sky/horizon content,
recorded on 2026-09-27 with the Debug x64 executable. They preserve the current
World Lighting appearance for comparison. They are separate from Coastal Atrium
Rendering Baseline 1 and do not establish quantitative BRDF, IBL or atmosphere
accuracy, deterministic replay, or pixel-comparison tolerances.

## Images

| Reference view | DirectX 12 | Vulkan |
| --- | --- | --- |
| `CAM_RoughnessSweep` | [Roughness sweep](../../GGLabLightingContract/PhysicalSky/roughness-sweep-dx12.png) | [Roughness sweep](../../GGLabLightingContract/PhysicalSky/roughness-sweep-vulkan.png) |
| `CAM_SkyHorizon` | [Sky and horizon](../../GGLabCoastalAtrium/PhysicalSky/sky-horizon-dx12.png) | [Sky and horizon](../../GGLabCoastalAtrium/PhysicalSky/sky-horizon-vulkan.png) |

| File | Capture time (UTC) | SHA-256 |
| --- | --- | --- |
| `roughness-sweep-dx12.png` | 2026-09-27 14:37:41 | `cdd874edcb764d9e560d237dcf8c79f77a6a28bee5767e046a711ff44de0c3ca` |
| `roughness-sweep-vulkan.png` | 2026-09-27 14:48:46 | `f5b55ac719582352fcdb1b4f6b31f0fd6858a4a3c9aa6736ea9afe0ddc28f433` |
| `sky-horizon-dx12.png` | 2026-09-27 14:46:15 | `bbc85dea9c92c5be66b9e30cc790a6e4301316ca5dfd0564def1147d4429228d` |
| `sky-horizon-vulkan.png` | 2026-09-27 14:51:23 | `4e399354c908bfc475d7bdaa31bef17303202d33e3fe93be4ecc1fb3f2537091` |

Computer Use captured the GGLab window with `get_window_state`. Its JPEG image
payload was decoded and saved as PNG. This does not recover lossless framebuffer
data. The raw window was 1922 by 1112 pixels; every saved image uses the same
unscaled crop `(x=1, y=50, width=1920, height=1060)`. The camera rendered at
1920 by 1080, aspect 16:9, before that crop. The crop excludes window borders,
the title bar, the top menu and the bottom border; it omits the menu-covered top
of the client image. DevTools panels were closed and the Computer Use pointer was
moved to the excluded title bar. Each saved PNG was inspected for pointer/glow
overlays and panel occlusion. No scene pixels were retouched or resampled.

## Render settings

| Setting | Value |
| --- | --- |
| Pipeline / display | Forward PBR, FinalColor, ACES fitted, SDR |
| Render dimensions / swapchain format | 1920 by 1080 / `R8G8B8A8UNorm` |
| Exposure | Manual EV100 15; compensation 0; scene pre-exposure enabled |
| TAA / GTAO / Bloom | Disabled / disabled / disabled |
| Sun | Physical World Sun; white chromaticity; TOA perpendicular illuminance 120000 lux; angular radius 0.2666 degrees |
| Atmosphere | Enabled; unmodified Earth defaults in `AtmosphereSettings` |
| Planet radius / center / scale | 6360000 m / `(0, -6360000, 0)` m / 1 m per world unit |
| Atmosphere / Rayleigh / Mie heights | 100000 / 8000 / 1200 m |
| Rayleigh scattering RGB | `(5.802e-6, 13.558e-6, 33.1e-6)` per meter |
| Mie scattering / extinction / anisotropy | `3.996e-6` / `4.44e-6` per meter / 0.8 |
| Absorption RGB / center / half width | `(0.65e-6, 1.881e-6, 0.085e-6)` per meter / 25000 / 15000 m |
| Ground albedo RGB | `(0.3, 0.3, 0.3)` |
| Sky source / skybox | Published Physical Sky / enabled |
| Environment intensity / yaw | Calibrated 1 / world-aligned 0 degrees |
| Source cubemap | `R32G32B32A32Float`, 512 by 512 by 6, 10 mip levels |
| IBL reference altitude / supported region | 1 m / local +Y, altitude 0-100 m within 1 km; camera motion does not rebake IBL |
| Published generation | Active = requested = 3 for sweep; 5 for horizon; retiring persistent textures = 0 |

The sweep enables pre-exposure through its Physical Sun preset; Atrium uses the
Demo's default enabled pre-exposure. No global render-setting override was added.
The sun directions below describe photon travel in the left-handed, Y-up Runtime
coordinate system. Positions, targets and clipping distances are in meters.

| Setting | Roughness Sweep | Sky / Horizon |
| --- | --- | --- |
| Active content | `Demo.LabHost`, `gglab.lab.lighting_contract` | `Demo.Playground.CoastalAtrium` |
| Camera / restored profile | `CAM_RoughnessSweep` / 2 | `CAM_SkyHorizon` / 1 |
| Position / target | `(48, 3.5, -13)` / `(48, 2, 0)` | `(20, 5.5, -26)` / `(0, 3, 0)` |
| Vertical FOV (radians) | 0.6509917105 | 0.7984415392 |
| Near / far | 0.1 / 100 | 0.1 / 150 |
| Sun direction | `normalize(0, -1, 1)` | `normalize(-1, -0.85, 0.35)` |
| Directional shadows | Disabled | Enabled; existing PCF settings |

## Build and content identity

The code working tree is based on `601ff3888603d7aa14e5790e3f8d753dee8f363d`.
It contains the reference-asset and camera-profile extension in its working
tree; the hashes below identify the installed exports and camera definitions.
Authoring-source fingerprints are provenance only; those files are not
distributed here and are not needed to reproduce the Runtime checks.
Runtime and shader sources have no working-tree changes relative to the code
base. The Git shader-tree identity is `065bc7ca5468dd014c1c7b2854711fd9756aa39d`.
Both runs used NVIDIA GeForce RTX 5080; Vulkan reported driver 617.14,
Vulkan API 1.4.351 and application baseline Vulkan 1.3. VSync was disabled;
Vulkan used Mailbox and DX12 allowed tearing. These captures are not timing data.

File paths in this table are relative to the code repository. Provenance rows
are fingerprints rather than paths to shipped files.

| Artifact | SHA-256 |
| --- | --- |
| `Build/Output/x64/Debug/GraphicsGadgetLab.exe` | `c67723c711651bc2f8eba13d2f2d40acf0237c1cda21efe8731ae7c93b0533f8` |
| `Build/Output/x64/Debug/dxcompiler.dll` | `9a5100511e127c6a2fc78edf984f95074a76d35b90c90c4d342430a5ae160e9b` |
| Lighting Contract authoring-source fingerprint (provenance) | `841bdbc7c7b7590d0aae269c61084778a73e6a26404fa32a336b9f913100f498` |
| Coastal Atrium authoring-source fingerprint (provenance) | `709cf13a6ef7b827736b7978c949608659624b519b882dc2519ae7920a9cbfaf` |
| `Assets/Models/GGLabLightingContract/GGLabLightingContract.gltf` | `602805e187a5197e1878e1403a883afafbc259ddf760166ed7328cfa889643b4` |
| `Assets/Models/GGLabLightingContract/GGLabLightingContract.bin` | `9ec30b41a3775e8f4f94d465dfe90105b3e0bed227fb423547c1085f774f2ecf` |
| `Assets/Models/GGLabCoastalAtrium/GGLabCoastalAtrium.gltf` | `7bcfc2ecf7e4f48446128d73af936d5147dafd8756d83ed56ce24ce892c14192` |
| `Assets/Models/GGLabCoastalAtrium/GGLabCoastalAtrium.bin` | `b31b36f65c0eb2aaca8ca280aa50ebe2446138bcf0ef4c957c4e4bcffbe52e7e` |
| `Sources/WinApp/Application/Lab/LightingContractReferenceViews.h` | `0f73dce55bae970a6f1eff352909f5873442fab9f6d83292b20eccfd7051843c` |
| `Sources/WinApp/Application/Demo/CoastalAtriumReferenceViews.h` | `954b9b147864a750ca991afbaa21e1c9e0588633b361f026748c1f0e5415562a` |

The DX12 program-registry identity was
`fa26ba420de1e4428740ebe3a4b41eaf40ad9aecde6a88d0a1d780caf0356369`
(registry file SHA-256 `f04f149d59f1aeb1857ca3f895a97a88fd7df4839713f2e8893eba99776bdba2`).
The Vulkan program-registry identity was
`2670fbbf8247ab0280c186334206b373898d921eb6739dd04f1eff8594231e53`
(registry file SHA-256 `0f89ddd804b5d9298357ba38f2c1c52d5d23c297e163a2ba4767c1cda6533f6d`).
Registry filenames identify the published shader programs; file hashes identify
their serialized bytes. Generated caches and binaries remain local build output.

The unchanged Atrium image hashes, relative to
`Assets/Models/GGLabCoastalAtrium/Textures/`, are:

| File | SHA-256 |
| --- | --- |
| `Concrete_BaseColor.png` | `e818a7ae00626effb5fcc87b52b9e9671cfb60555d5ea6630a2d6ae79e65410d` |
| `Concrete_MetallicRoughness.png` | `4c86c67ae2978d6d92b7b2c5e95f4ac8ba8d70fa9131dd1e8bf1a4095493a2e9` |
| `Concrete_Normal.png` | `3cfc07edbd853b63a489d4badb4852891447779c51012bede6362c2db83dcb89` |
| `Metal_BaseColor.png` | `d8a5bf06a09c0056ec7e9eac0adf71e513d5658b00627acf2902634e87faf73a` |
| `Metal_MetallicRoughness.png` | `931c557ce52a15ee6c0f581fe4aee34bff96e3dcc00b6254af6d1f42e337962a` |
| `Metal_Normal.png` | `d1e7cc1c0be239458ab8bcda525c08b05da90cd4ff0d540ad5a89b925b24811d` |
| `Stone_BaseColor.png` | `430e72861fc0aac6d2f28da426c505fce244cc4e629c0e71ab5c858d51acd86b` |
| `Stone_MetallicRoughness.png` | `5d8f81d00944a33837b4e88e312ba9bbf18b7e679fbea6c8b0707acb06530c72` |
| `Stone_Normal.png` | `b1d701b9a1c5482e887ac25f6a9d7969ee10ca8a8ff036b2bc7442665e909bac` |

## Reproduce and inspect

Run from the code repository root with separate state directories:

```powershell
./Build/Output/x64/Debug/GraphicsGadgetLab.exe --lab gglab.lab.lighting_contract --rhi dx12 --absolute-mouse --state-root ./Build/ContentReferenceChecks/StateCaptureDX12
./Build/Output/x64/Debug/GraphicsGadgetLab.exe --lab gglab.lab.lighting_contract --rhi vulkan --absolute-mouse --state-root ./Build/ContentReferenceChecks/StateCaptureVulkan
```

1. In Lab Control, select Roughness Sweep and enable Physical Sun. Enable
   Atmosphere, choose Physical Sky and enable the skybox in World Lighting.
   Wait for the Physical Sky publication to finish and confirm the settings above.
2. Close the panels, place the pointer in the title bar and capture the window.
3. Switch through Application > Demo to Coastal Atrium. In Scene > Camera,
   select Sky / Horizon. Set Manual EV100 to 15 after restoring the reference:
   its profile deliberately restores legacy 0 EV. In World Lighting, enable
   Physical Sun and Atmosphere, retain Physical Sky, and enable the skybox.
4. Confirm publication and close the panels before capturing the second view.
   Apply the documented crop and inspect the saved artifact for overlays.

All twelve sweep spheres are visible, with roughness 0, 0.05, 0.1, 0.25, 0.5, 1
from left to right; the upper row is metallic and the lower row is dielectric.
Sky, architecture and shoreline composition match visually between backends.
The black band between the physical sky horizon and the finite ocean's far edge
is visible on both. Dark reflected lower hemispheres and existing shadow artifacts
are preserved; these captures do not fix sky/ground completion or add distant
aerial-perspective geometry.

Both runs completed presentation, switched content and exited normally with
code 0. Logs contained no assertion, shader compilation, GPU lifetime or device
loss errors. The Vulkan diagnostics inspected during the sweep reported zero
validation errors/warnings and healthy runtime state. The existing startup HDR
FP16 sanitization warning remains (16 channels clamped to 65000 in
`golden_gate_hills_2k.hdr`); the final images use the published Physical Sky.
Vulkan's loader also emitted informational messages about an AMD layer export;
these were not validation errors. Local logs are
`Build/ContentReferenceChecks/capture-dx12.log` and `capture-vulkan.log`.
