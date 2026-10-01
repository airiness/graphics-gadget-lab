#include <PBR/DirectLighting.hlsli>

// Frozen pre-refactor integration: keep its separate loops and branch order.
// This compares the production HLSL response, including layer composition,
// against the original algorithm without a CPU reimplementation.
float3 ReferenceSunDiskSpecular(float3 centerDirection, float3 N, float3 V, float3 F0,
	float physicalRoughness, float angularRadius)
{
	const float sineRadius = sin(angularRadius);
	const float sineRadiusSquared = sineRadius * sineRadius;
	const float3 referenceAxis = abs(centerDirection.y) < 0.99 ?
		float3(0.0, 1.0, 0.0) : float3(1.0, 0.0, 0.0);
	const float3 tangent = normalize(cross(referenceAxis, centerDirection));
	const float3 bitangent = cross(centerDirection, tangent);
	const float NoV = saturate(dot(N, V));
	float3 integrated = 0.0.xxx;
	[unroll]
	for (uint sampleIndex = 0; sampleIndex < 32u; ++sampleIndex)
	{
		const float radial = sqrt((sampleIndex + 0.5) / 32.0 * sineRadiusSquared);
		const float phase = sampleIndex * 2.39996322973;
		const float3 L = centerDirection * sqrt(1.0 - radial * radial) +
			(tangent * cos(phase) + bitangent * sin(phase)) * radial;
		const float NoL = saturate(dot(N, L));
		const float3 H = SafeNormalize(L + V, N);
		const float NoH = saturate(dot(N, H));
		const float VoH = saturate(dot(V, H));
		integrated += D_GGX(NoH, physicalRoughness) *
			V_SmithGGXCorrelated(NoV, NoL, physicalRoughness) *
			F_Schlick(F0, 1.0.xxx, VoH) * NoL;
	}
	// Solid-angle samples reconstruct the authored perpendicular disk illuminance.
	return integrated * (2.0 / (1.0 + cos(angularRadius))) / 32.0;
}

float3 ReferenceSunDiskAnisotropicSpecular(float3 centerDirection, float3 N, float3 V,
	float3 F0, AnisotropyShadingState anisotropy, float angularRadius)
{
	const float sineRadius = sin(angularRadius);
	const float sineRadiusSquared = sineRadius * sineRadius;
	const float3 referenceAxis = abs(centerDirection.y) < 0.99 ?
		float3(0.0, 1.0, 0.0) : float3(1.0, 0.0, 0.0);
	const float3 tangent = normalize(cross(referenceAxis, centerDirection));
	const float3 bitangent = cross(centerDirection, tangent);
	float3 integrated = 0.0.xxx;
	[unroll]
	for (uint sampleIndex = 0; sampleIndex < 32u; ++sampleIndex)
	{
		const float radial = sqrt((sampleIndex + 0.5) / 32.0 * sineRadiusSquared);
		const float phase = sampleIndex * 2.39996322973;
		const float3 L = centerDirection * sqrt(1.0 - radial * radial) +
			(tangent * cos(phase) + bitangent * sin(phase)) * radial;
		const float NoL = saturate(dot(N, L));
		const float3 H = SafeNormalize(L + V, N);
		const float VoH = saturate(dot(V, H));
		integrated += D_GGXAnisotropic(H, N, anisotropy.TangentWS,
			anisotropy.BitangentWS, anisotropy.AlphaT, anisotropy.AlphaB) *
			V_SmithGGXCorrelatedAnisotropic(V, L, N, anisotropy.TangentWS,
				anisotropy.BitangentWS, anisotropy.AlphaT, anisotropy.AlphaB) *
			F_Schlick(F0, 1.0.xxx, VoH) * NoL;
	}
	// The same 32 solid-angle samples and perpendicular-illuminance normalization
	// as the isotropic world-sun lobe keep the two paths comparable.
	return integrated * (2.0 / (1.0 + cos(angularRadius))) / 32.0;
}

float3 ReferenceClearcoatDirect(float3 L, float3 V, ClearcoatShadingState coat,
	bool worldSun, float angularRadius)
{
	const float NoL = saturate(dot(coat.NormalWS, L));
	if (NoL <= 0.0) return 0.0.xxx;
	const float3 F0 = 0.04.xxx;
	if (worldSun && coat.BRDFAlpha < 0.04)
	{
		// Integrate the finite solar disk with the coat's own normal and GGX width.
		return ReferenceSunDiskSpecular(L, coat.NormalWS, V, F0, coat.BRDFAlpha, angularRadius) *
			coat.EnergyCompensation;
	}
	const float3 H = SafeNormalize(L + V, coat.NormalWS);
	const float NoH = saturate(dot(coat.NormalWS, H));
	const float VoH = saturate(dot(V, H));
	return D_GGX(NoH, coat.BRDFAlpha) *
		V_SmithGGXCorrelated(coat.NoV, NoL, coat.BRDFAlpha) *
		F_Schlick(F0, 1.0.xxx, VoH) * coat.EnergyCompensation * NoL;
}

float3 ReferenceDirectMaterialResponse(float3 L, float3 V, float3 N,
	float NoV, float NoL, float coatNoL, float3 F0, float brdfAlpha,
	float3 diffuse, float3 energyCompensation, AnisotropyShadingState anisotropy,
	ClearcoatShadingState coat, bool worldSun, float angularRadius)
{
	const float3 H = SafeNormalize(L + V, N);
	const float NoH = saturate(dot(N, H));
	const float VoH = saturate(dot(V, H));
	float D;
	float visibility;
	if (anisotropy.Strength > 0.0)
	{
		D = D_GGXAnisotropic(H, N, anisotropy.TangentWS, anisotropy.BitangentWS,
			anisotropy.AlphaT, anisotropy.AlphaB);
		visibility = V_SmithGGXCorrelatedAnisotropic(V, L, N, anisotropy.TangentWS,
			anisotropy.BitangentWS, anisotropy.AlphaT, anisotropy.AlphaB);
	}
	else
	{
		D = D_GGX(NoH, brdfAlpha);
		visibility = V_SmithGGXCorrelated(NoV, NoL, brdfAlpha);
	}
	const float3 F = F_Schlick(F0, 1.0.xxx, VoH);
	const float3 specular = D * visibility * F * energyCompensation;
	float3 response = (diffuse + specular) * NoL;
	if (worldSun && (anisotropy.Strength > 0.0 ? anisotropy.AlphaB : brdfAlpha) < 0.04)
	{
		const float3 diskSpecular = anisotropy.Strength > 0.0
			? ReferenceSunDiskAnisotropicSpecular(L, N, V, F0, anisotropy, angularRadius)
			: ReferenceSunDiskSpecular(L, N, V, F0, brdfAlpha, angularRadius);
		response = diffuse * NoL + diskSpecular * energyCompensation;
	}
	if (coat.Factor > 0.0)
	{
		const float incidentReflectance = F_Schlick(0.04.xxx, 1.0.xxx, coatNoL).x;
		const float transmission = (1.0 - coat.Factor * coat.DirectionalAlbedo) *
			(1.0 - coat.Factor * incidentReflectance);
		response = response * transmission + coat.Factor *
			ReferenceClearcoatDirect(L, V, coat, worldSun, angularRadius);
	}
	return response;
}

#ifndef GGLAB_SUN_DISK_TEST_CASE
#define GGLAB_SUN_DISK_TEST_CASE 0
#endif

float4 PSMain() : SV_Target
{
	float3 N = float3(0.0, 1.0, 0.0);
	float3 L = normalize(float3(0.05, 1.0, 0.03));
	float3 V = normalize(float3(-0.04, 1.0, -0.02));
	float brdfAlpha = 0.01;
	float angularRadius = 0.004712389;
	bool worldSun = true;
	AnisotropyShadingState anisotropy;
	anisotropy.Strength = 0.0;
	anisotropy.TangentWS = float3(1.0, 0.0, 0.0);
	anisotropy.BitangentWS = float3(0.0, 0.0, 1.0);
	anisotropy.AlphaT = 0.025;
	anisotropy.AlphaB = 0.004;
	ClearcoatShadingState coat;
	coat.Factor = 0.6;
	coat.NormalWS = normalize(float3(0.015, 1.0, -0.02));
	coat.PerceptualRoughness = 0.08;
	coat.BRDFAlpha = 0.0064;
	coat.DirectionalAlbedo = 0.09;
	coat.EnergyCompensation = float3(1.05, 1.05, 1.05);

#if GGLAB_SUN_DISK_TEST_CASE == 0
	coat.Factor = 0.0;
#elif GGLAB_SUN_DISK_TEST_CASE == 1
	anisotropy.Strength = 0.7;
	coat.Factor = 0.0;
#elif GGLAB_SUN_DISK_TEST_CASE == 2
	// Smooth isotropic base plus an independent smooth coat normal.
#elif GGLAB_SUN_DISK_TEST_CASE == 3
	anisotropy.Strength = 0.7;
#elif GGLAB_SUN_DISK_TEST_CASE == 4
	brdfAlpha = 0.2;
#elif GGLAB_SUN_DISK_TEST_CASE == 5
	coat.BRDFAlpha = 0.2;
#elif GGLAB_SUN_DISK_TEST_CASE == 6
	brdfAlpha = 0.04;
	coat.BRDFAlpha = 0.04;
#elif GGLAB_SUN_DISK_TEST_CASE == 7
	brdfAlpha = 0.039999;
	coat.BRDFAlpha = 0.040001;
#elif GGLAB_SUN_DISK_TEST_CASE == 8
	brdfAlpha = 0.040001;
	coat.BRDFAlpha = 0.039999;
#elif GGLAB_SUN_DISK_TEST_CASE == 9
	anisotropy.Strength = 0.7;
	anisotropy.AlphaB = 0.04;
#elif GGLAB_SUN_DISK_TEST_CASE == 10
	worldSun = false;
	anisotropy.Strength = 0.7;
#elif GGLAB_SUN_DISK_TEST_CASE == 11
	coat.NormalWS = -N;
#elif GGLAB_SUN_DISK_TEST_CASE == 12
	L = normalize(float3(1.0, -0.001, 0.0));
	V = normalize(float3(-1.0, 0.01, 0.0));
	coat.NormalWS = normalize(float3(0.02, 1.0, 0.0));
#elif GGLAB_SUN_DISK_TEST_CASE == 13
	angularRadius = 0.0174532925;
	anisotropy.Strength = 0.7;
#elif GGLAB_SUN_DISK_TEST_CASE == 14
	// L + V is below SafeNormalize's threshold. Each lobe must retain
	// its own fallback even though the solar sampling directions are shared.
	L = normalize(float3(1.0, 0.000001, 0.0));
	V = normalize(float3(-1.0, 0.000001, 0.0));
	coat.NormalWS = normalize(float3(0.0, 1.0, 1.0));
	angularRadius = 0.0;
#elif GGLAB_SUN_DISK_TEST_CASE == 15
	L = normalize(float3(1.0, 0.4, 0.2));
	V = normalize(float3(-1.0, 0.4, -0.2));
	anisotropy.Strength = 0.7;
	anisotropy.TangentWS = float3(0.6, 0.0, 0.8);
	anisotropy.BitangentWS = float3(0.8, 0.0, -0.6);
#else
#error Unknown sun-disk contract case
#endif

	const float NoV = saturate(dot(N, V));
	const float NoL = saturate(dot(N, L));
	coat.NoV = saturate(dot(coat.NormalWS, V));
	const float coatNoL = saturate(dot(coat.NormalWS, L));
	const float3 F0 = float3(0.04, 0.15, 0.7);
	const float3 diffuse = float3(0.1, 0.2, 0.3);
	const float3 compensation = float3(1.1, 1.3, 1.8);
	const float3 reference = ReferenceDirectMaterialResponse(L, V, N, NoV, NoL,
		coatNoL, F0, brdfAlpha, diffuse, compensation, anisotropy, coat, worldSun, angularRadius);
	PreparedMaterialShading material = (PreparedMaterialShading)0;
	material.Base.NormalWS = N;
	material.Base.F0 = F0;
	material.Base.BRDFAlpha = brdfAlpha;
	material.ViewDirectionWS = V;
	material.NoV = NoV;
	material.Anisotropy = anisotropy;
	material.Clearcoat = coat;
	material.EnergyCompensation = compensation;
	// Convert the frozen diffuse BRDF into the prepared color/weight inputs.
	material.BaseColor = diffuse * PI;
	material.DiffuseWeight = 1.0.xxx;
	const float3 actual = EvaluateDirectMaterialResponse(L, NoL, coatNoL,
		material, worldSun, angularRadius);
	// Bit checks constant-fold in DXIL; IsFinite remains a runtime intrinsic
	// even for literal inputs in the current compiler.
	const bool finite = all((asuint(actual) & 0x7f800000u) != 0x7f800000u) &&
		all((asuint(reference) & 0x7f800000u) != 0x7f800000u);
	const bool matches = finite &&
		all(abs(actual - reference) <= 1.0e-6.xxx + abs(reference) * 1.0e-5);
	return matches ? 0.0.xxxx : 1.0.xxxx;
}

