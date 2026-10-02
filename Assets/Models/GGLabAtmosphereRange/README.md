# Atmosphere Range

Original atmosphere-range fixture exported using Blender 5.1.1
(`b70da489d7f4`, glTF exporter 5.1.19). The [bundle contract](../README.md)
describes installed-content validation. SHA-256 identifies the exported asset
pair; the authoring-source fingerprint is retained as provenance only:

| File | SHA-256 |
| --- | --- |
| Authoring-source fingerprint (provenance) | `83c4f0aa846de53645f8031915990dc870f71a0a3bfdff110b2c59cdfb306222` |
| `GGLabAtmosphereRange.gltf` | `19ef62b3fdb8a8e55978bc257d4cf92e5cb530c087ee52e7cfbce679e633ea96` |
| `GGLabAtmosphereRange.bin` | `80c279bae9ac1cea83f7515cedb0175a9d25a5885448032c92f47f681cae4c30` |

The glTF/bin pair has 18 mesh patches, three constant materials and seven cameras,
with no textures or imported lights. Assimp may merge meshes/materials; numeric
spatial inputs remain authoritative. Imported geometry is in meters, Y-up,
with front normals `(0,0,-1)`.

```powershell
GraphicsGadgetLab.exe --rhi dx12 --lab gglab.lab.atmosphere_range --absolute-mouse
GraphicsGadgetLab.exe --rhi vulkan --lab gglab.lab.atmosphere_range --absolute-mouse
```

Select reference views through Lab Control. `CAM_AerialRange` shows identical
6 m squares at gray-patch center-ray distances 25, 50, 100, 250, 500, 1000 m,
left to right. Six `CAM_Range_<distance>m` views frame individual targets from
the same observer `(0,20,0)`; use these for interior samples. All use near/far
0.1 / 2000 m, 16:9 reference aspect and EV100 15/compensation 0. Patch inputs
are linear RGB 0.02, 0.18, 0.90, metallic 0, roughness 1, opaque, non-emissive.

The Lab activates Physical Sky, default Earth atmosphere and one 120,000 lux
world sun with ray direction `normalize(0,-1,1)`. Sky/IBL intensity is 1, yaw 0;
pre-exposure is on and TAA/GTAO/bloom/shadows start off. Environment overrides
apply on session entry and restore on exit, and model ownership follows normal
Lab preparation/cancellation/fence retirement. The Runtime **Aerial Perspective**
parameter skips only surface transport, so an off/on pair preserves resolved
sun, sky, IBL and exposure. Toggling it or TAA requests a temporal reset.

Numeric transport acceptance needs each target's own linear off/on samples,
recorded pre-exposure and unchanged active physical-sky generation. Directions
and altitude vary with target placement, and ordinary PBR is not a constant
surface-radiance reference. SDR captures and authoring PNGs are visual evidence
only. This integration does not establish a GPU numeric transport error bound.

[DX12/Vulkan on/off and TAA capture record](../../Media/WorldLighting/AtmosphereRangeReferences/CAPTURE.md).
