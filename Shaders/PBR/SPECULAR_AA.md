# Specular anti-aliasing contract

`SpecularAA.hlsli` measures the screen-space derivative of the resolved world-space
normal. The squared `ddx` and `ddy` lengths form normal variance. The filter adds
`min(2 * variance, 0.18)` to GGX alpha, after clamping authored perceptual
roughness to the BRDF minimum. Authored roughness and imported material data are
never modified. The scale and cap preserve the existing Forward PBR response.

The base GGX lobe uses one effective perceptual roughness for direct lights,
finite-sun integration, the BRDF LUT, energy compensation and environment mip
selection. Clearcoat uses the same filter with its own resolved normal and
roughness; its direct, finite-sun and IBL paths share that result. Anisotropic
GGX constructs its tangent and bitangent alpha from authored base roughness,
then adds the base-normal footprint to each axis. The two axes remain distinct
until both reach the roughness ceiling. Its existing isotropic LUT and
environment prefilter are approximations, using the filtered base roughness.

Charlie sheen retains its independent authored roughness. The GGX alpha
footprint has not been calibrated for the grazing microfiber lobe or its
directional-albedo fit. Filtering the base or clearcoat does not silently change
sheen. A Charlie-specific spatial filter requires separate energy and visual
validation before being enabled.

The material debug views expose authored and effective base roughness,
authored and effective clearcoat roughness, and anisotropic alpha (tangent in
red, bitangent in green). `Normal Variance` shows base in red and active coat
in green. `Specular AA` shows additive base alpha in red, active coat alpha in
green and the base perceptual-roughness increase in blue. `Lit (Specular AA
Off)` sets the filter contribution to zero for an image comparison. It still
evaluates derivatives, so it is not a shader-cost baseline.

In Mini PBR Grid, `NormalTangentTest` copies its single imported material into
a Lab-owned instance so the debug view can change without mutating the asset.
Inspect it at near and far distances and grazing angles with TAA disabled and
enabled, then compare `Lit` with `Lit (Specular AA Off)` on DX12 and Vulkan.
Use GPU timings from the backend profiler for performance claims; the debug
comparison alone only demonstrates the spatial image effect. For the base-path
cost baseline, leave clearcoat factor, anisotropy strength and sheen color at
zero in the procedural grid; enable each optional layer separately and compare
the Forward PBR pass GPU timing at the same camera and lighting settings.
