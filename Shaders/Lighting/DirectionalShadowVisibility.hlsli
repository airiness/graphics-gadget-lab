#pragma once
#include <Lighting/ShadowSampling.hlsli>
#include <Lighting/DirectionalShadowData.hlsli>

float EvaluateDirectionalShadowCascade(float3 positionWS, ShadowReceiverPlane receiver,
	float receiverNoL, uint cascadeIndex, DirectionalShadowData shadow,
	Texture2DArray<float> shadowMap, SamplerComparisonState shadowSampler, uint shadowMapSize, bool pcf)
{
	const uint viewIndex = shadow.ViewBaseIndex + cascadeIndex;
	const ShadowProjection projection = ProjectToShadowMap(positionWS, viewIndex);
	if (!projection.IsValid)
	{
		return 1.0;
	}
	const float2 texelSize = 1.0.xx / max((float)(shadowMapSize), 1.0);
	const float2 gradient = ComputeShadowReceiverDepthGradient(receiver, viewIndex);
	const float residualBias = EvaluateDirectionalShadowReceiverBias(cascadeIndex, receiverNoL, shadow);
	// Hardware comparison filters cover four texels; PCF offsets follow the receiver plane.
	const float bilinearBias = ShadowBilinearReceiverBias(gradient.x, gradient.y, texelSize.x, texelSize.y);
	const float compareDepth = projection.ReceiverDepth - residualBias - bilinearBias;
	if (!pcf)
	{
		return SampleShadowHard(shadowMap, shadowSampler, projection.UV, cascadeIndex, saturate(compareDepth));
	}
	return SampleShadowPCF3x3(shadowMap, shadowSampler, projection.UV, cascadeIndex,
		compareDepth, texelSize, gradient);
}

float EvaluateDirectionalShadowVisibility(float3 positionWS, ShadowReceiverPlane receiver,
	float receiverNoL, DirectionalShadowData shadow, Texture2DArray<float> shadowMap,
	SamplerComparisonState shadowSampler, uint shadowMapSize, bool pcf)
{
	const float mainViewZ = TransformPositionVS(float4(positionWS, 1.0), LoadViewData(shadow.MainViewIndex)).z;
	const uint cascadeIndex = SelectDirectionalShadowCascade(mainViewZ, shadow);
	if (cascadeIndex >= shadow.CascadeCount)
	{
		return 1.0;
	}
	float visibility = EvaluateDirectionalShadowCascade(positionWS, receiver, receiverNoL,
		cascadeIndex, shadow, shadowMap, shadowSampler, shadowMapSize, pcf);
	const float blendStart = shadow.BlendStart[cascadeIndex];
	const float blendWidth = shadow.SplitFar[cascadeIndex] - blendStart;
	if (cascadeIndex + 1u < shadow.CascadeCount && blendWidth > 0.0 && mainViewZ > blendStart)
	{
		const float next = EvaluateDirectionalShadowCascade(positionWS, receiver, receiverNoL,
			cascadeIndex + 1u, shadow, shadowMap, shadowSampler, shadowMapSize, pcf);
		visibility = lerp(visibility, next, smoothstep(blendStart, shadow.SplitFar[cascadeIndex], mainViewZ));
	}
	const float fade = saturate((mainViewZ - shadow.DistanceFadeStart) * shadow.DistanceFadeInvRange);
	return lerp(visibility, 1.0, fade * fade * (3.0 - 2.0 * fade));
}
