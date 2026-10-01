#pragma once
#include <PBR/MaterialShading.hlsli>

static const uint WorldSunDiskSampleCount = 32u;
static const float WorldSunDiskAlphaThreshold = 0.04;

struct SunDiskSampling
{
	float3 CenterDirection;
	float3 Tangent;
	float3 Bitangent;
	float SineRadiusSquared;
	float IlluminanceNormalization;
};

SunDiskSampling BuildSunDiskSampling(float3 centerDirection, float angularRadius)
{
	SunDiskSampling sampling;
	sampling.CenterDirection = centerDirection;
	const float3 referenceAxis = abs(centerDirection.y) < 0.99 ?
		float3(0.0, 1.0, 0.0) : float3(1.0, 0.0, 0.0);
	sampling.Tangent = normalize(cross(referenceAxis, centerDirection));
	sampling.Bitangent = cross(centerDirection, sampling.Tangent);
	const float sineRadius = sin(angularRadius);
	sampling.SineRadiusSquared = sineRadius * sineRadius;
	sampling.IlluminanceNormalization = 2.0 / (1.0 + cos(angularRadius));
	return sampling;
}

float3 SampleSunDiskDirection(SunDiskSampling sampling, uint sampleIndex)
{
	const float radial = sqrt((sampleIndex + 0.5) / (float) WorldSunDiskSampleCount *
		sampling.SineRadiusSquared);
	const float phase = sampleIndex * 2.39996322973;
	return sampling.CenterDirection * sqrt(1.0 - radial * radial) +
		(sampling.Tangent * cos(phase) + sampling.Bitangent * sin(phase)) * radial;
}

float3 EvaluateIsotropicDirectBRDF(float3 L, float3 V, float3 N,
	float NoV, float NoL, float3 F0, float brdfAlpha)
{
	const float3 H = SafeNormalize(L + V, N);
	const float NoH = saturate(dot(N, H));
	const float VoH = saturate(dot(V, H));
	return D_GGX(NoH, brdfAlpha) * V_SmithGGXCorrelated(NoV, NoL, brdfAlpha) *
		F_Schlick(F0, 1.0.xxx, VoH);
}

float3 EvaluateBaseDirectBRDF(float3 L, float3 V, float3 N,
	float NoV, float NoL, float3 F0, float brdfAlpha, AnisotropyShadingState anisotropy)
{
	if (anisotropy.Strength <= 0.0)
	{
		return EvaluateIsotropicDirectBRDF(L, V, N, NoV, NoL, F0, brdfAlpha);
	}
	const float3 H = SafeNormalize(L + V, N);
	const float VoH = saturate(dot(V, H));
	return D_GGXAnisotropic(H, N, anisotropy.TangentWS, anisotropy.BitangentWS,
		anisotropy.AlphaT, anisotropy.AlphaB) *
		V_SmithGGXCorrelatedAnisotropic(V, L, N, anisotropy.TangentWS,
			anisotropy.BitangentWS, anisotropy.AlphaT, anisotropy.AlphaB) *
		F_Schlick(F0, 1.0.xxx, VoH);
}

struct DirectSpecularLobes
{
	float3 Base;
	float3 Clearcoat;
};

DirectSpecularLobes IntegrateSunDiskSpecular(SunDiskSampling sampling,
	float3 N, float3 V, float NoV, float3 F0, float brdfAlpha,
	AnisotropyShadingState anisotropy, ClearcoatShadingState coat,
	bool integrateBase, bool integrateClearcoat)
{
	DirectSpecularLobes integrated;
	integrated.Base = 0.0.xxx;
	integrated.Clearcoat = 0.0.xxx;
	[unroll]
	for (uint sampleIndex = 0; sampleIndex < WorldSunDiskSampleCount; ++sampleIndex)
	{
		// Both lobes see the same solar disk. Their normals, GGX widths and
		// SafeNormalize half-vector fallbacks remain independent.
		const float3 L = SampleSunDiskDirection(sampling, sampleIndex);
		if (integrateBase)
		{
			const float NoL = saturate(dot(N, L));
			integrated.Base += EvaluateBaseDirectBRDF(L, V, N, NoV, NoL,
				F0, brdfAlpha, anisotropy) * NoL;
		}
		if (integrateClearcoat)
		{
			const float NoL = saturate(dot(coat.NormalWS, L));
			integrated.Clearcoat += EvaluateIsotropicDirectBRDF(L, V, coat.NormalWS,
				coat.NoV, NoL, 0.04.xxx, coat.BRDFAlpha) * NoL;
		}
	}
	// Preserve the existing perpendicular-illuminance normalization and
	// accumulation order separately for each lobe.
	integrated.Base = integrated.Base * sampling.IlluminanceNormalization / (float) WorldSunDiskSampleCount;
	integrated.Clearcoat = integrated.Clearcoat * sampling.IlluminanceNormalization / (float) WorldSunDiskSampleCount;
	return integrated;
}

float3 EvaluateDirectMaterialResponse(float3 L, float NoL, float coatNoL,
	PreparedMaterialShading material, bool worldSun, float sunAngularRadius)
{
	const BaseShadingState base = material.Base;
	const AnisotropyShadingState anisotropy = material.Anisotropy;
	const ClearcoatShadingState coat = material.Clearcoat;
	const float3 V = material.ViewDirectionWS;
	const float3 diffuse = material.DiffuseWeight * Fd_Lambert(material.BaseColor);
	// The center approximation applies at alpha >= 0.04. An anisotropic base
	// uses its narrower width; the coat keeps its own center-visibility gate.
	const bool integrateBase = worldSun &&
		(anisotropy.Strength > 0.0 ? anisotropy.AlphaB : base.BRDFAlpha) < WorldSunDiskAlphaThreshold;
	const bool integrateClearcoat = worldSun && coat.Factor > 0.0 && coatNoL > 0.0 &&
		coat.BRDFAlpha < WorldSunDiskAlphaThreshold;
	DirectSpecularLobes disk;
	disk.Base = 0.0.xxx;
	disk.Clearcoat = 0.0.xxx;
	if (integrateBase || integrateClearcoat)
	{
		disk = IntegrateSunDiskSpecular(BuildSunDiskSampling(L, sunAngularRadius),
			base.NormalWS, V, material.NoV, base.F0, base.BRDFAlpha, anisotropy, coat,
			integrateBase, integrateClearcoat);
	}

	float3 response;
	if (integrateBase)
	{
		response = diffuse * NoL + disk.Base * material.EnergyCompensation;
	}
	else
	{
		const float3 specular = EvaluateBaseDirectBRDF(L, V, base.NormalWS, material.NoV, NoL,
			base.F0, base.BRDFAlpha, anisotropy) * material.EnergyCompensation;
		response = (diffuse + specular) * NoL;
	}
	if (coat.Factor > 0.0)
	{
		float3 coatResponse = 0.0.xxx;
		if (integrateClearcoat)
		{
			coatResponse = disk.Clearcoat * coat.EnergyCompensation;
		}
		else if (coatNoL > 0.0)
		{
			coatResponse = EvaluateIsotropicDirectBRDF(L, V, coat.NormalWS,
				coat.NoV, coatNoL, 0.04.xxx, coat.BRDFAlpha) * coat.EnergyCompensation * coatNoL;
		}
		// Two interface crossings attenuate the base before adding the coat.
		const float incidentReflectance = F_Schlick(0.04.xxx, 1.0.xxx, coatNoL).x;
		const float transmission = (1.0 - coat.Factor * coat.DirectionalAlbedo) *
			(1.0 - coat.Factor * incidentReflectance);
		response = response * transmission + coat.Factor * coatResponse;
	}
	return response;
}
