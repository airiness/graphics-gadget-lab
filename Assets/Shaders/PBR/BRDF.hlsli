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

// Height-correlated Smith visibility term for GGX.
// Approximates the combined masking and shadowing effect for view and light directions.
float V_SmithGGXCorrelated(float NoV, float NoL, float a)
{
	float a2 = a * a;
	float GGXL = NoV * sqrt((-NoL * a2 + NoL) * NoL + a2);
	float GGXV = NoL * sqrt((-NoV * a2 + NoV) * NoV + a2);
	return 0.5 / (GGXV + GGXL);
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