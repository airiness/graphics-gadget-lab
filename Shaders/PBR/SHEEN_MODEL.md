# Sheen shading contract

`KHR_materials_sheen` adds a microfiber reflection above the metallic-roughness
base and below clearcoat. A zero color factor skips both sheen texture samples.
A color texture that resolves to zero skips roughness sampling. Either case
disables Charlie evaluation and the environment lookup.

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
the published visibility fit can integrate above one there. The base response
is scaled by a conservative analytic estimate of sheen directional albedo;
the estimate uses the greater of the view and light directions. The World Sun
uses 32 solid-angle samples for narrow sheen, matching the finite-disk policy
used for narrow GGX lobes.

`Scripts/ValidateSheenEnergy.py` integrates the direct response over a white
hemisphere at 266 view/roughness points. It checks that the normalized sheen
albedo stays below one and that the energy estimate covers it. This is a
numeric contract for the approximation; it does not establish visual quality
for an arbitrary environment.

## Environment response and layers

Sheen IBL reuses the existing isotropic GGX environment prefilter at sheen
roughness, sampled in the view/backscatter direction. It is multiplied by the
same directional energy estimate that reserves energy from the base IBL. This
is a roughness-aware approximation, not a Charlie-prefiltered environment.
It introduces no new baked LUT or prefilter artifact. A future dedicated
Charlie prefilter would need its own versioned bake and cache contract.

Clearcoat attenuates the combined base and sheen response before adding its
own reflection. Ambient occlusion affects sheen IBL in the same way as the
base specular IBL. `Sheen Color`, `Sheen Roughness` and `Sheen Contribution`
diagnostics expose the resolved inputs and the reflected sheen term.
