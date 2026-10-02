#pragma once
#include <Common/Common.hlsli>

static const float MIN_PERCEPTUAL_ROUGHNESS = 0.045;

// Clamp perceptual roughness ensure it not zero.
float ClampPerceptualRoughnessForBRDF(float perceptualRoughness)
{
	return clamp(perceptualRoughness, MIN_PERCEPTUAL_ROUGHNESS, 1.0);
}

// Converts perceptual roughness to GGX microfacet alpha.
float PerceptualRoughnessToAlpha(float perceptualRoughness)
{
	return perceptualRoughness * perceptualRoughness;
}

// GGX / Trowbridge-Reitz normal distribution function.
// a is the microfacet alpha parameter converted from perceptual roughness.
float D_GGX(float NoH, float a)
{
	float a2 = a * a;
	float f = (NoH * a2 - NoH) * NoH + 1.0;
	float denom = PI * f * f;

	return a2 / max(denom, 1e-12);
}

// Schlick Fresnel approximation.
// Interpolates between F0 at normal incidence and F90 at grazing angles.
float3 F_Schlick(float3 F0, float3 F90, float cosTheta)
{
	return F0 + (F90 - F0) * Pow5(1.0 - cosTheta);
}

// The split-sum LUT stores A = integral(1 - Fc) and B = integral(Fc).
// A + B is the single-scattering directional albedo at F0 = 1.
// Filament's DFG approximation restores the missing energy with
// 1 + F0 * (1 / directionalAlbedo - 1).
float3 GGXEnergyCompensation(float3 F0, float2 brdfLUT)
{
	const float directionalAlbedo = saturate(brdfLUT.x + brdfLUT.y);
	if (directionalAlbedo <= 1.0e-4)
	{
		// An unbaked LUT cannot provide a trustworthy energy estimate.
		return 1.0.xxx;
	}
	return 1.0.xxx + saturate(F0) * (rcp(directionalAlbedo) - 1.0);
}

// Height-correlated Smith visibility term for GGX.
// Approximates the combined masking and shadowing effect for view and light directions.
float V_SmithGGXCorrelated(float NoV, float NoL, float a)
{
	if (NoV <= 0.0 || NoL <= 0.0)
	{
		return 0.0;
	}

	float a2 = a * a;
	float GGXL = NoV * sqrt((-NoL * a2 + NoL) * NoL + a2);
	float GGXV = NoL * sqrt((-NoV * a2 + NoV) * NoV + a2);
	return 0.5 / max(GGXV + GGXL, 1e-6);
}

float D_GGXAnisotropic(float3 H, float3 N, float3 T, float3 B, float alphaT, float alphaB)
{
	const float NoH = saturate(dot(N, H));
	if (NoH <= 0.0) return 0.0;
	const float ToH = dot(T, H) / alphaT;
	const float BoH = dot(B, H) / alphaB;
	const float denominator = ToH * ToH + BoH * BoH + NoH * NoH;
	return rcp(max(PI * alphaT * alphaB * denominator * denominator, 1.0e-12));
}

float V_SmithGGXCorrelatedAnisotropic(float3 V, float3 L, float3 N,
	float3 T, float3 B, float alphaT, float alphaB)
{
	const float NoV = saturate(dot(N, V));
	const float NoL = saturate(dot(N, L));
	if (NoV <= 0.0 || NoL <= 0.0) return 0.0;
	const float viewLength = length(float3(alphaT * dot(T, V), alphaB * dot(B, V), NoV));
	const float lightLength = length(float3(alphaT * dot(T, L), alphaB * dot(B, L), NoL));
	return 0.5 / max(NoL * viewLength + NoV * lightLength, 1.0e-6);
}

// Lambertian diffuse BRDF.
// Converts diffuse color / albedo to a constant diffuse reflectance over the hemisphere.
float3 Fd_Lambert(float3 DiffuseColor)
{
	return DiffuseColor * (1.0 / PI);
}

float3 ImportanceSampleGGX(float2 Xi, float a)
{
	float a2 = a * a;

	float phi = 2.0 * PI * Xi.x;

	float cosTheta = sqrt((1.0 - Xi.y) / (1.0 + (a2 - 1.0) * Xi.y));
	float sinTheta = sqrt(max(0.0, 1.0 - cosTheta * cosTheta));

	float3 H;
	H.x = cos(phi) * sinTheta;
	H.y = sin(phi) * sinTheta;
	H.z = cosTheta;

	return H;
}
