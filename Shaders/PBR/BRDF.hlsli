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

// Charlie microfiber distribution and visibility for glTF sheen.
float D_Charlie(float NoH, float alpha)
{
	const float inverseAlpha = rcp(max(alpha, 0.002));
	const float sinThetaSquared = saturate(1.0 - NoH * NoH);
	return (2.0 + inverseAlpha) * pow(sinThetaSquared, 0.5 * inverseAlpha) /
		(2.0 * PI);
}

float CharlieLambdaFit(float cosine, float alpha)
{
	const float oneMinusAlphaSquared = (1.0 - alpha) * (1.0 - alpha);
	const float a = lerp(21.5473, 25.3245, oneMinusAlphaSquared);
	const float b = lerp(3.82987, 3.32435, oneMinusAlphaSquared);
	const float c = lerp(0.19823, 0.16801, oneMinusAlphaSquared);
	const float d = lerp(-1.97760, -1.27393, oneMinusAlphaSquared);
	const float e = lerp(-4.32054, -4.85967, oneMinusAlphaSquared);
	return a / (1.0 + b * pow(cosine, c)) + d * cosine + e;
}

float CharlieLambda(float cosine, float alpha)
{
	return cosine < 0.5 ? exp(CharlieLambdaFit(cosine, alpha)) :
		exp(2.0 * CharlieLambdaFit(0.5, alpha) - CharlieLambdaFit(1.0 - cosine, alpha));
}

float V_Charlie(float NoV, float NoL, float alpha)
{
	if (NoV <= 0.0 || NoL <= 0.0) return 0.0;
	return rcp(max((1.0 + CharlieLambda(NoV, alpha) + CharlieLambda(NoL, alpha)) *
		4.0 * NoV * NoL, 1.0e-6));
}

// The Charlie visibility fit can integrate above one at very smooth grazing
// angles. The correction must be symmetric in the two directions so the BRDF
// remains reciprocal.
float CharlieSheenNormalization(float NoV, float NoL, float perceptualRoughness)
{
	const float smoothness = 1.0 - perceptualRoughness;
	return 1.0 + 5.0 * smoothness * smoothness * smoothness * smoothness *
		exp(-min(NoV, NoL) / 0.02);
}

// Chebyshev fit to the reciprocal Charlie BRDF integrated over a white
// hemisphere. Prepare its roughness-dependent rows once per shaded pixel;
// direct lights then evaluate only the view-axis polynomial.
void PrepareSheenDirectionalAlbedo(float perceptualRoughness,
	out float4 fitLow, out float3 fitHigh)
{
	const float y = 2.0 * ClampPerceptualRoughnessForBRDF(perceptualRoughness) - 1.0;
	float ty[7];
	ty[0] = 1.0;
	ty[1] = y;
	[unroll]
	for (uint index = 2u; index < 7u; ++index)
	{
		ty[index] = 2.0 * y * ty[index - 1u] - ty[index - 2u];
	}
	static const float coefficients[49] =
	{
		0.4662008734, 0.1421159035, 0.05200258402, -0.05125628079, 0.006825325422, -0.004605599776, 0.002613252967,
		-0.4067666558, 0.06915140574, -0.1195616388, 0.1024846881, -0.04368122318, 0.02249280341, -0.01160481628,
		-0.04041981132, -0.1105422938, 0.1245469551, -0.1063839573, 0.06729718862, -0.03703415321, 0.02134558657,
		0.06907690518, -0.04916388234, 0.003350617615, 0.02625226625, -0.02702190985, 0.02009724266, -0.01436163036,
		-0.03666481275, 0.04569041403, -0.05400284215, 0.04947314302, -0.03266691141, 0.01681106069, -0.006804344716,
		0.002019475767, 0.008140849677, 0.01012986063, -0.02356920128, 0.02254739397, -0.0177240175, 0.01245039065,
		0.009723827867, -0.02778271004, 0.01832648501, -0.01192787424, 0.008339719302, -0.004460323995, 0.001011952775
	};
	float rows[7];
	[unroll]
	for (uint row = 0u; row < 7u; ++row)
	{
		rows[row] = 0.0;
		[unroll]
		for (uint column = 0u; column < 7u; ++column)
		{
			rows[row] += coefficients[row * 7u + column] * ty[column];
		}
	}
	fitLow = float4(rows[0], rows[1], rows[2], rows[3]);
	fitHigh = float3(rows[4], rows[5], rows[6]);
}

// The positive bias bounds the sampled fit residual while preserving the
// existing two-channel GGX DFG texture contract.
float SheenDirectionalAlbedo(float NoV, float4 fitLow, float3 fitHigh)
{
	const float x = 2.0 * log(1.0 + max(NoV, 0.001) * 50.0) / log(51.0) - 1.0;
	float next = 0.0;
	float nextNext = 0.0;
	[unroll]
	for (int row = 6; row > 0; --row)
	{
		const float coefficient = row >= 4 ? fitHigh[row - 4] : fitLow[row];
		const float current = coefficient + 2.0 * x * next - nextNext;
		nextNext = next;
		next = current;
	}
	return saturate(fitLow.x + x * next - nextNext + 0.026);
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
