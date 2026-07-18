#pragma once
#include <Common/VertexTransform.hlsli>

struct ShadowProjection
{
	float2 UV;
	float ReceiverDepth;
	bool IsValid;
};

ShadowProjection ProjectToShadowMap(float3 positionWS, uint shadowViewIndex)
{
	ShadowProjection projection;
	projection.UV = 0.0.xx;
	projection.ReceiverDepth = 0.0;
	projection.IsValid = false;

	const ViewData shadowView = LoadViewData(shadowViewIndex);

	const float4 shadowPosVS = TransformPositionVS(float4(positionWS, 1.0), shadowView);
	const float4 shadowPosCS = TransformPositionCS(shadowPosVS, shadowView);
	if (shadowPosCS.w <= 0.0)
	{
		return projection;
	}

	const float3 shadowNDC = shadowPosCS.xyz / shadowPosCS.w;
	projection.UV = shadowNDC.xy * float2(0.5, -0.5) + 0.5;
	projection.ReceiverDepth = shadowNDC.z;
	projection.IsValid =
		projection.UV.x > 0.0 && projection.UV.x < 1.0 &&
		projection.UV.y > 0.0 && projection.UV.y < 1.0 &&
		projection.ReceiverDepth > 0.0 && projection.ReceiverDepth < 1.0;

	return projection;
}

float SampleShadowHard(
	Texture2D<float> shadowMap,
	SamplerComparisonState shadowSampler,
	float2 uv,
	float compareDepth)
{
	return shadowMap.SampleCmpLevelZero(shadowSampler, uv, compareDepth);
}

float SampleShadowPCF3x3(
	Texture2D<float> shadowMap,
	SamplerComparisonState shadowSampler,
	float2 uv,
	float compareDepth,
	float2 texelSize)
{
	float visibility = 0.0;

	[unroll]
	for (int y = -1; y <= 1; ++y)
	{
		[unroll]
		for (int x = -1; x <= 1; ++x)
		{
			const float2 offset = float2((float) x, (float) y) * texelSize;
			visibility += SampleShadowHard(shadowMap, shadowSampler, uv + offset, compareDepth);
		}
	}

	return visibility * (1.0 / 9.0);
}
