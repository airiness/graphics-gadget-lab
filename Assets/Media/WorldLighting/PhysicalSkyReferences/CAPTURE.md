# Physical Sky reference captures

SDR references captured on September 27, 2026 (UTC), showing the
[Lighting Contract](../../../Models/GGLabLightingContract/README.md) roughness
sweep and [Coastal Atrium](../../../Models/GGLabCoastalAtrium/README.md) horizon.

## Images

| Reference view | DirectX 12 | Vulkan |
| --- | --- | --- |
| `CAM_RoughnessSweep` | [Roughness sweep](../../GGLabLightingContract/PhysicalSky/roughness-sweep-dx12.png) | [Roughness sweep](../../GGLabLightingContract/PhysicalSky/roughness-sweep-vulkan.png) |
| `CAM_SkyHorizon` | [Sky and horizon](../../GGLabCoastalAtrium/PhysicalSky/sky-horizon-dx12.png) | [Sky and horizon](../../GGLabCoastalAtrium/PhysicalSky/sky-horizon-vulkan.png) |

## Render settings

| Setting | Value |
| --- | --- |
| Pipeline / display | Forward PBR, FinalColor, ACES fitted, SDR |
| Render / image dimensions | 1920 x 1080 / 1920 x 1060 cropped |
| Exposure | Manual EV100 15; compensation 0; pre-exposure enabled |
| TAA / GTAO / bloom | Disabled |
| Sun | Physical World Sun; white; 120000 lux; angular radius 0.2666 degrees |
| Atmosphere | Enabled; Earth defaults |
| Sky source / skybox | Physical Sky / enabled |
| IBL intensity / yaw | Calibrated 1 / 0 degrees |

Camera coordinates use left-handed Y-up meters; sun directions describe photon
travel. Images are compressed SDR references.

| View | Position / target | Vertical FOV, radians | Near / far, meters | Sun direction | Shadows |
| --- | --- | --- | --- | --- | --- |
| `CAM_RoughnessSweep` | `(48, 3.5, -13)` / `(48, 2, 0)` | 0.6509917105 | 0.1 / 100 | `normalize(0, -1, 1)` | Disabled |
| `CAM_SkyHorizon` | `(20, 5.5, -26)` / `(0, 3, 0)` | 0.7984415392 | 0.1 / 150 | `normalize(-1, -0.85, 0.35)` | Enabled; PCF |

Machine-readable [capture evidence](capture-evidence.json) retains file identities,
method/crop metadata and recorded diagnostics.

## Observations

- The sweep shows six roughness levels in metallic and dielectric rows.
- Sky and shoreline composition are visually consistent between DX12 and Vulkan.

## Known limitations

- The finite ocean leaves a black gap below the physical horizon.
- SDR screenshots do not establish quantitative BRDF or atmosphere accuracy.
