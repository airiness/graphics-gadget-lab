# Coastal Atrium Concrete Surface

SDR references captured on October 2, 2026 (UTC), showing refined concrete.
Geometry and other materials match the preceding
[coastal detail captures](../CoastalDetails/CAPTURE.md). See the
[asset reference](../../../Models/GGLabCoastalAtriumResearchLounge/README.md)
for the scene contract.

## Images

| Reference view | DirectX 12 | Vulkan |
| --- | --- | --- |
| Courtyard | [Image](courtyard-dx12.png) | [Image](courtyard-vulkan.png) |
| Shadow Stairs | [Image](shadow-stairs-dx12.png) | [Image](shadow-stairs-vulkan.png) |
| Interior / Exterior | [Image](interior-exterior-dx12.png) | [Image](interior-exterior-vulkan.png) |
| Sky / Horizon | [Image](sky-horizon-dx12.png) | [Image](sky-horizon-vulkan.png) |

## Render settings

| Setting | Value |
| --- | --- |
| Scene | `Demo.Playground.CoastalAtrium` |
| Render / image dimensions | 1920 x 1080 / 1920 x 1010 cropped |
| Pipeline / lighting | Forward PBR / Forward+ |
| Sun | Physical World Sun; 120000 lux; angular radius 0.2666 degrees |
| Sun ray direction | `normalize(-1, -0.85, 0.35)` |
| Atmosphere / skybox | Earth defaults / Physical Sky enabled |
| IBL intensity / yaw | Calibrated 1 / 0 degrees |
| Exposure | Manual EV100 15; compensation 0 |
| TAA / GTAO / bloom | Disabled |
| Shadows | Default directional PCF |

Camera coordinates use left-handed Y-up meters, with near/far 0.1/150 m.
Images are compressed SDR references with window chrome and pointer excluded.

| View | Position | Target | Vertical FOV, degrees |
| --- | --- | --- | --- |
| Courtyard | `(23, 19, -28)` | `(-1, 1.8, -2)` | 37.2990761 |
| Shadow Stairs | `(5.5, 3.4, -14)` | `(0, 2.8, 1)` | 39.7607002 |
| Interior / Exterior | `(-12, 4, -2.9)` | `(2, 2.5, -5)` | 45.7473259 |
| Sky / Horizon | `(20, 5.5, -26)` | `(0, 3, 0)` | 45.7473259 |
