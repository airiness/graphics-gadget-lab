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

## Specular environment visibility

Build WinApp and ShaderCompiler, then run the hidden, dual-RHI suite:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File Tests/Verification/RunVerification.ps1 -Suite gi-specular-visibility -Configuration Develop -Rhi dx12,vulkan -Python <python-with-numpy-jsonschema-and-pillow>
```

`gglab.lab.gi_reference` fixture 10 provides dedicated GGX visibility contracts,
separate from F1-F10. Activate it through RestartSession before assigning the
Immediate `gi_reference.visibility_material` parameter; `visibility_geometry`
also requires RestartSession. Other `gi_reference.*` controls are `samples`,
`seed`, `batch_samples` and `tile_pixels`.

Runtime configuration uses Public
[GISpecularVisibilityCreateInfo](../../../Sources/GGLabRuntime/Public/GGLabRuntime/Graphics/Pipeline/GISpecularVisibility.h)
with a complete `PrimaryReceiver` RayScene. This opt-in diagnostic leaves
production S0 and diffuse reference sequences unchanged.

Read channel/identity semantics from the `GI.SpecularVisibility.*` export metadata.
Receiver arrays and `IBL.BrdfLut` use separate bounded groups. Capture readiness
means GPU calculation completed, not numerical convergence; the independent
`ValidateGISpecularVisibility.py` checker determines numerical qualification.

Fixture 11 collects Coastal diagnostics at locked C1-C4 views and 1280x800.
Unmatched pixels remain reported; these outputs are not reference-certified.
The standalone `TestGISpecularVisibility.ps1` supports `-Smoke` and `-SkipCoastal`;
neither establishes the full suite. Detailed measurement contracts and
qualification records belong to `gglab-docs` GI plan §§11.16–11.17.

## Irradiance reference

After building WinApp and ShaderCompiler, run:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File Tests/Verification/RunVerification.ps1 -Suite gi-reference -Configuration Develop -Rhi dx12,vulkan -Python <python-with-numpy-and-jsonschema>
```

The hidden `gglab.lab.gi_reference` Lab constructs the frozen F1 isolated sphere
and plane and F2 six-solid, 1 m thick sealed room. All fixture controls use the
LabRuntime RestartSession path. It locks Native, unjittered rendering with
TAA/GTAO/bloom off. Seeds 61051/61052 and 64/128 samples exercise independent and
doubled estimates; F1 white/tinted albedo and EV100 0/10/15 variants must leave
exported irradiance exactly unchanged. Missing/unmatched/unsupported receivers
fail rather than disappearing from measurements.

`GI.Reference.Irradiance` is scene-referred E, without receiver albedo,
pre-exposure or the primary receiver's direct sun. `GI.Reference.LambertRadiance`
is the reduced diffuse diagnostic `albedo * E / pi`, not the complete Forward
material or Final. The reference does not read SceneColor, IBL irradiance, a field,
a denoiser or temporal history. Each observation resumes its fixed-seed sequence
across bounded submissions. Environment misses require complete all-World bounds
and `t_max >= bounds diagonal`.

The suite exports eight float arrays and the same-frame graph per observation.
`ValidateGIReference.py` checks the frozen inputs, finite values and named
primitive/world-space masks; it writes `measurement-mask.png` overlays and
`numerical-results.json`. F1 and F2's exterior roof require mean E within 1% of
pi L. F2 interior masks require exactly zero irradiance and zero sky misses.
A secondary backface at a touching-solid seam remains an occluded hit, never sky;
its count is reported. Primary lookup follows raster sidedness at those seams.
Fixture normal scale is zero so the RGBA8 flat-normal fallback cannot introduce a
normal-map tilt. The checker has headless tests:

```powershell
python Tests/Verification/ValidateGIReference.py --self-test
```

See [ReferenceLighting.md](ReferenceLighting.md) for the Sun/Sky fixtures, filter
checks, batching and capture-readiness contracts. Reference measurements do not
establish field quality, Final or performance acceptance.

Ray-scene and GI-off checks cover membership regression, cache reuse and disabled
behavior, not exhaustive native traversal coverage. Reference checks establish
only the analytic fixture scope above, not overall GI quality.
Fixture design and baseline reproduction notes live in `gglab-docs`:
`GGLab_Global_Illumination_Fixture_Specification.md` and
`GGLab_Global_Illumination_Design_And_Plan.md` section 11.2. The fixture version/hash
in `Inputs.json` records documentation provenance; validation needs no docs checkout.
