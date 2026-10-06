# Material response reference

The reviewed World Lighting code baseline is merge commit
`1f941027bfffdf8e12e8e4f55767c64d711923f8` (PR #194). This reference
records the material response retained while the BRDF evolves.

The current single-scattering GGX response is measured in
`GGLabRuntimeTests --suite rendering-contracts` with a Runtime-owned, unit
uniform environment and 512 polar midpoint samples. With F0 = 1, the
directional albedo decreases as perceptual roughness rises from 0.25 to 0.5 to
1.0; at roughness 1.0 it approaches `1 - ln(2) = 0.30685`. The test evaluates
scene-linear values before exposure, environment prefiltering, and tone mapping.
It also checks the current BRDF LUT interpretation (`F0 * A + B`),
roughness-to-alpha conversion, and the IOR 1.5 default (`F0 = 0.04`).

The [Physical Sky references](PhysicalSkyReferences/CAPTURE.md) provide manual
SDR appearance examples for DX12 and Vulkan. The capture record lists
lighting settings and camera poses. These images do not measure
linear-light BRDF energy.

Material emissive factors retain the pre-World-Lighting legacy scale.

The [Material Shading captures](MaterialReferences/CAPTURE.md) archive the
2026-10-02 DX12/Vulkan clearcoat, anisotropy and Research Lounge appearance,
including TAA-off Lit/UnfilteredLit comparisons and Specular AA diagnostics.
These SDR stills complement the contract tests; they do not measure BRDF energy
or motion stability.
