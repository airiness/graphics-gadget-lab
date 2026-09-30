#pragma once
#include <PBR/BRDF.hlsli>

// Squared normal derivatives form the pixel footprint. Add its scaled, capped variance
// to GGX alpha (roughness squared), then convert back to perceptual roughness;
// authored roughness stays unchanged and TAA remains a separate filter.
static const float SpecularAAVarianceScale = 2.0;
static const float SpecularAAMaxKernelAlpha = 0.18;

struct SpecularAAResult
{
	float NormalVariance;
	float KernelAlpha;
	float EffectivePerceptualRoughness;
};

SpecularAAResult EvaluateSpecularAA(float authoredPerceptualRoughness, float3 normalWS,
	bool enabled)
{
	const float3 normalDx = ddx(normalWS);
	const float3 normalDy = ddy(normalWS);
	SpecularAAResult result;
	result.NormalVariance = dot(normalDx, normalDx) + dot(normalDy, normalDy);
	// The comparison view suppresses the contribution, but still pays for derivatives.
	const float enabledScale = enabled ? 1.0 : 0.0;
	result.KernelAlpha = min(SpecularAAVarianceScale * result.NormalVariance,
		SpecularAAMaxKernelAlpha) * enabledScale;
	const float authoredAlpha = PerceptualRoughnessToAlpha(
		ClampPerceptualRoughnessForBRDF(authoredPerceptualRoughness));
	result.EffectivePerceptualRoughness = sqrt(saturate(authoredAlpha + result.KernelAlpha));
	return result;
}

float2 FilterAnisotropicAlpha(float authoredBaseAlpha, float strength, float kernelAlpha)
{
	// Broaden both axes by the same normal footprint after constructing the
	// authored lobe. Filtering the base first would shrink its anisotropic span.
	const float authoredAlphaT = lerp(authoredBaseAlpha, 1.0, strength * strength);
	return saturate(float2(authoredAlphaT, authoredBaseAlpha) + kernelAlpha.xx);
}
