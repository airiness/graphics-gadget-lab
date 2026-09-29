# Sheen shading contract

`KHR_materials_sheen` adds a microfiber reflection above the metallic-roughness
base and below clearcoat. A zero color factor skips sheen texture sampling in
the lit path. If a nonzero factor is multiplied by a color texture that
resolves to zero, roughness is still sampled: a texture result must not guard
another implicit-derivative sample. A zero resolved color disables Charlie
evaluation and the environment lookup. The roughness diagnostic can sample its
texture even when the sheen color factor is zero.

## Imported inputs

The color factor is linear RGB and multiplies an optional sRGB color texture.
The perceptual roughness factor multiplies the alpha channel of an optional
linear texture. Each binding retains its own UV set, `KHR_texture_transform` and
sampler. Missing textures supply multiplicative white. The roughness remains
independent of base and clearcoat roughness.

## Direct response and energy

Direct lights use the Charlie microfiber distribution and visibility from the
glTF sheen implementation notes. The squared perceptual roughness is clamped
to the existing BRDF minimum. Very smooth grazing angles are normalized because
the published visibility fit can integrate above one there. The normalization
uses the smaller of `NoV` and `NoL`, preserving BRDF reciprocity. The base
response uses the greater of the view and light directional albedos, matching
the glTF albedo-scaling form. The World Sun uses 32 solid-angle samples for
narrow sheen, applying the symmetric normalization per sample.

`Scripts/ValidateSheenEnergy.py` integrates the reciprocal direct response
over a white hemisphere at 500 grid and midpoint cases. It checks reciprocity,
the conservative analytic validation bound, the shader's fitted directional
albedo, and white-environment layered energy. CI runs this numeric contract.
The sixth-order Chebyshev fit uses 49 coefficients and a small positive bias;
it was fitted to a 16-by-19 view/roughness grid with 256-by-256 midpoint
hemisphere integration and is evaluated on a separate midpoint grid in CI.
The validation grid bounds its directional-albedo error to 0.08 and layered
white-environment response to 1.03. These bounds do not establish visual
quality for an arbitrary environment.

## Environment response and layers

Sheen IBL reuses the existing isotropic GGX environment prefilter at sheen
roughness, sampled in the view/backscatter direction. It is multiplied by the
same fitted directional albedo that reserves energy from the base IBL. This is
a roughness-aware approximation, not a Charlie-prefiltered environment.
It introduces no new baked LUT or prefilter artifact. A future dedicated
Charlie prefilter would need its own versioned bake and cache contract.

Clearcoat attenuates the combined base and sheen response before adding its
own reflection. Ambient occlusion affects sheen IBL in the same way as the
base specular IBL. `Sheen Color`, `Sheen Roughness` and `Sheen Contribution`
diagnostics expose the resolved inputs and the reflected sheen term.
