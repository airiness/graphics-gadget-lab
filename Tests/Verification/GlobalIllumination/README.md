# GI qualification inputs

[Inputs.json](Inputs.json) locks the Coastal asset, cameras, lighting, field layout,
quality gates and adapter budgets. The retained
[Daylight baseline](../../../Assets/Media/GGLabCoastalRetreat/GI/Daylight/README.md)
provides pre-GI regression images.

Run from the code repository root:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File Tests/Verification/RunVerification.ps1 -Suite gi-inputs
```

This checks local inputs and capture identities; it does not qualify GI rendering.

The Coastal ray-scene regression uses the locked references in WinAppHost's
`Application/Lab/Validation/CoastalRaySceneOracle.h`. The Lab validates 22 fixed
hits, two misses and the complete integer geometry/instance table digests on GPU
readback. The `coastal-ray-scene-oracle` WinAppTests suite independently intersects
the imported CPU triangles and checks adversarial oracle failures.

After building the application and shader compiler, run the hidden GPU suite
with Python `numpy` and `jsonschema` available:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File Tests/Verification/RunVerification.ps1 -Suite coastal-ray-scene -Configuration Develop -Rhi dx12,vulkan
```

The suite also exports `RayScene.SunVisibility` and opaque depth for all four
locked views, with TAA jitter disabled. The RGBA32F channels contain binary RT
visibility, filtered CSM visibility, receiver state and first accepted blocker
distance in metres. Export metadata describes background, unmatched and miss
values. `ValidateCoastalSunVisibility.py` checks finite values, receiver coverage
and at least 99.5% cross-RHI agreement; unmatched receivers count as disagreement.
CSM differences are reported separately from that gate.

The Coastal Lab also enables Runtime GI receiver diagnostics at Native resolution
with TAA off. The suite captures receiver/material and shading records in separate
bounded resource groups. `ValidateCoastalGIMaterial.py` checks coverage, the three
normal meanings, production diffuse/specular inputs and reduced GI albedo decoded
from the committed triangle. It repeats the material check after entity recycling
and compares both RHIs. Each export group explicitly names/restores its locked
reference view. Material agreement must reach 99.5% over opaque receiver coverage
with maximum RGB albedo error 0.003 and vertex-normal dot product at least 0.999;
unmatched receivers count against the gate. These exports are unexposed surface
data, not GI radiance. Run the numerical checker's positive/adversarial self-tests
without a GPU using `python Tests/Verification/ValidateCoastalGIMaterial.py --self-test`.

These checks cover membership regression, cache reuse and disabled behavior. They
do not establish exhaustive native traversal coverage or GI quality.
Fixture design and baseline reproduction notes live in `gglab-docs`:
`GGLab_Global_Illumination_Fixture_Specification.md` and
`GGLab_Global_Illumination_Design_And_Plan.md` section 11.2. The fixture version/hash
in `Inputs.json` records documentation provenance; validation needs no docs checkout.
