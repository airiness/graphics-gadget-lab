# Atmosphere Range references

SDR comparisons of surface aerial perspective in the
[Atmosphere Range Lab](../../../Models/GGLabAtmosphereRange/README.md).
The paired 1 km images differ only in the Aerial Perspective setting.

## Images

| Backend | 1 km, aerial perspective on | 1 km, aerial perspective off | 25 m, TAA on |
| --- | --- | --- | --- |
| DirectX 12 | [On](dx12-1000m-aerial-on.png) | [Off](dx12-1000m-aerial-off.png) | — |
| Vulkan | [On](vulkan-1000m-aerial-on.png) | [Off](vulkan-1000m-aerial-off.png) | [Image](vulkan-25m-taa-on.png) |

## Render settings

| Setting | Value |
| --- | --- |
| Reference cameras | `CAM_Range_01000m`; `CAM_Range_00025m` for the short-path image |
| Render / image dimensions | 1920 x 1080 / 1920 x 1060 cropped |
| Exposure | Manual EV100 15; compensation 0; pre-exposure enabled |
| Sun | Physical World Sun; 120000 lux; `normalize(0, -1, 1)` |
| Atmosphere / sky | Earth defaults / Physical Sky |
| IBL intensity / yaw | 1 / 0 degrees |
| GTAO / bloom / shadows | Disabled |
| TAA | Disabled for the 1 km pair; enabled for the 25 m image |

Lab controls remain visible in these compressed SDR images.
