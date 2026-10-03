# Coastal Atrium Surface Materials

SDR references captured on October 3, 2026 (UTC), showing refined paving,
satin structure metal and woven upholstery. See the
[installed asset reference](../../../Models/GGLabCoastalAtriumResearchLounge/README.md)
and [reference camera guide](CAPTURE_GUIDE.md).

## Images

| Reference view | DirectX 12 |
| --- | --- |
| Courtyard | [Image](courtyard-dx12.png) |
| Shadow Stairs | [Image](shadow-stairs-dx12.png) |
| Paving Surface | [Image](paving-surface-dx12.png) |
| Railing Detail | [Image](railing-detail-dx12.png) |
| Upholstery Surface | [Image](upholstery-surface-dx12.png) |
| Upholstery Weave | [Image](upholstery-weave-dx12.png) |

Matching Vulkan images remain pending.

## Render settings

| Setting | Value |
| --- | --- |
| Scene | `Demo.Playground.CoastalAtrium` |
| Render / image dimensions | 1920 x 1080 / 1920 x 1010 cropped |
| Pipeline / material view | Forward PBR, Forward+ / Lit; default Specular AA |
| Sun | Physical World Sun; 120000 lux; angular radius 0.2666 degrees |
| Sun ray direction | `normalize(-1, -0.85, 0.35)` |
| Atmosphere / skybox | Earth defaults / Physical Sky enabled |
| IBL intensity / yaw | Calibrated 1 / 0 degrees |
| Exposure | Manual EV100 15; compensation 0 |
| TAA / GTAO / bloom | Disabled |
| Shadows | Default directional PCF |

Images are compressed SDR references with window chrome and pointer excluded.
