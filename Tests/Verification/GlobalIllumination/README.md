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
Fixture design and baseline reproduction notes live in `gglab-docs`:
`GGLab_Global_Illumination_Fixture_Specification.md` and
`GGLab_Global_Illumination_Design_And_Plan.md` section 11.2. The fixture version/hash
in `Inputs.json` records documentation provenance; validation needs no docs checkout.
