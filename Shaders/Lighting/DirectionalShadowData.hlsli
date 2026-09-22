#pragma once

// Matches DirectionalShadowGPU; indices are relative to SceneCB.ViewBaseIndex.
struct DirectionalShadowData
{
	float4 SplitFar;
	uint ViewBaseIndex;
	uint CascadeCount;
	uint MainViewIndex;
	float NearDepth;
};

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
