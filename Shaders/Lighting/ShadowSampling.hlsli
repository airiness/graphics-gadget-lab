#pragma once
#include <Common/VertexTransform.hlsli>
#include <Lighting/ShadowReceiverPlaneMath.hlsli>

struct ShadowProjection
{
	float2 UV;
	float ReceiverDepth;
	bool IsValid;
};

struct ShadowReceiverPlane
{
	float3 PositionDx;
	float3 PositionDy;
	float3 NormalWS;
};

ShadowReceiverPlane BuildShadowReceiverPlane(float3 positionWS)
{
	ShadowReceiverPlane plane;
	// Evaluate before material discard, light loops and cascade-dependent control flow.
	plane.PositionDx = ddx(positionWS);
	plane.PositionDy = ddy(positionWS);
	const float3 normal = cross(plane.PositionDx, plane.PositionDy);
	plane.NormalWS = normal * rsqrt(max(dot(normal, normal), 1e-30));
	return plane;
}

float2 ComputeShadowReceiverDepthGradient(ShadowReceiverPlane plane, uint shadowViewIndex)
{
	const ViewData view = LoadViewData(shadowViewIndex);
	const float3 dx = mul(mul(float4(plane.PositionDx, 0.0), view.ViewMat), view.ProjMat).xyz;
	const float3 dy = mul(mul(float4(plane.PositionDy, 0.0), view.ViewMat), view.ProjMat).xyz;
	const float2 uvDx = dx.xy * float2(0.5, -0.5);
	const float2 uvDy = dy.xy * float2(0.5, -0.5);
	const float determinant = uvDx.x * uvDy.y - uvDx.y * uvDy.x;
	// A light-edge-on triangle has no invertible receiver plane in shadow UV.
	const float scale = max(length(uvDx) * length(uvDy), 1e-30);
	if (abs(determinant) <= scale * 1e-6)
	{
		return 0.0.xx;
	}
	return float2(dx.z * uvDy.y - dy.z * uvDx.y,
		uvDx.x * dy.z - uvDy.x * dx.z) / determinant;
}

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
	Texture2DArray<float> shadowMap, SamplerComparisonState shadowSampler, float2 uv, uint layer, float compareDepth)
{
	return shadowMap.SampleCmpLevelZero(shadowSampler, float3(uv, layer), compareDepth);
}

float SampleShadowPCF3x3(Texture2DArray<float> shadowMap, SamplerComparisonState shadowSampler,
	float2 uv, uint layer, float compareDepth, float2 texelSize, float2 receiverDepthGradient)
{
	float visibility = 0.0;

	[unroll]
	for (int y = -1; y <= 1; ++y)
	{
		[unroll]
		for (int x = -1; x <= 1; ++x)
		{
			const float2 offset = float2((float) x, (float) y) * texelSize;
			visibility += SampleShadowHard(shadowMap, shadowSampler, uv + offset, layer,
				saturate(OffsetShadowReceiverDepth(compareDepth, receiverDepthGradient.x,
					receiverDepthGradient.y, offset.x, offset.y)));
		}
	}

	return visibility * (1.0 / 9.0);
}
