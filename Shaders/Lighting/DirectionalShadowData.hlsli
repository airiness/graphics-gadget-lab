#pragma once

// Matches DirectionalShadowGPU; indices are relative to SceneCB.ViewBaseIndex.
struct DirectionalShadowData
{
	float4 SplitFar;
	uint ViewBaseIndex;
	uint CascadeCount;
	uint MainViewIndex;
	float NearDepth;
	float4 ReceiverDepthBias;
	float4 ReceiverSlopeDepthBias;
	float4 ReceiverMaxSlope;
	float4 BlendStart;
	uint ReceiverPlaneCorrection;
	float DistanceFadeStart;
	float DistanceFadeInvRange;
	float ShadowMetadataPadding;
};

float EvaluateDirectionalShadowReceiverBias(uint cascadeIndex, float receiverNoL,
	DirectionalShadowData shadow)
{
	// Use the geometric receiver normal; authored and normal-map normals do not define depth.
	// The bounded tangent avoids an unbounded offset at grazing incidence.
	const float cosine = saturate(abs(receiverNoL));
	const float slope = min(sqrt(saturate(1.0 - cosine * cosine)) / max(cosine, 0.001),
		shadow.ReceiverMaxSlope[cascadeIndex]);
	return shadow.ReceiverDepthBias[cascadeIndex] + shadow.ReceiverSlopeDepthBias[cascadeIndex] * slope;
}

uint SelectDirectionalShadowCascade(float mainViewZ, DirectionalShadowData shadow)
{
	if (mainViewZ < shadow.NearDepth)
	{
		return shadow.CascadeCount;
	}
	for (uint index = 0; index < shadow.CascadeCount; ++index)
	{
		if (mainViewZ <= shadow.SplitFar[index])
		{
			return index;
		}
	}
	return shadow.CascadeCount;
}
