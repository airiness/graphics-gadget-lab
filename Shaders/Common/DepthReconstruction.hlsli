#pragma once
#include <Common/ScreenSpace.hlsli>

// ABI: values must match gglab::DepthConvention.
static const uint DEPTH_CONVENTION_REVERSED = 0;
static const uint DEPTH_CONVENTION_STANDARD = 1;

float GetDepthBackgroundValue(uint convention)
{
	return convention == DEPTH_CONVENTION_REVERSED ? 0.0 : 1.0;
}

float GetDepthNearValue(uint convention)
{
	return convention == DEPTH_CONVENTION_REVERSED ? 1.0 : 0.0;
}

float GetDepthFarValue(uint convention)
{
	return GetDepthBackgroundValue(convention);
}

bool IsDepthBackground(float rawDepth, uint convention)
{
	return convention == DEPTH_CONVENTION_REVERSED ? rawDepth <= 0.0 : rawDepth >= 1.0;
}

bool IsDepthNearer(float lhs, float rhs, uint convention)
{
	return convention == DEPTH_CONVENTION_REVERSED ? lhs > rhs : lhs < rhs;
}

bool IsDepthFarther(float lhs, float rhs, uint convention)
{
	return convention == DEPTH_CONVENTION_REVERSED ? lhs < rhs : lhs > rhs;
}

float3 ReconstructViewPosition(float2 uv, float rawDepth, float4x4 inverseProjection)
{
	return ReconstructPositionFromRawDepth(uv, rawDepth, inverseProjection);
}

float3 ReconstructWorldPosition(float2 uv, float rawDepth, float4x4 inverseViewProjection)
{
	return ReconstructPositionFromRawDepth(uv, rawDepth, inverseViewProjection);
}

float RawDepthToPositiveViewZ(float2 uv, float rawDepth, float4x4 inverseProjection)
{
	return max(ReconstructViewPosition(uv, rawDepth, inverseProjection).z, 0.0);
}

float RawDepthToPositiveViewZ(
	float rawDepth, float4 reconstructionParams, uint convention)
{
	if (convention != DEPTH_CONVENTION_REVERSED &&
		convention != DEPTH_CONVENTION_STANDARD)
	{
		return 0.0;
	}

	const float denominator = rawDepth * reconstructionParams.y - reconstructionParams.x;
	const float numerator = reconstructionParams.z - rawDepth * reconstructionParams.w;
	if (!isfinite(numerator) || !isfinite(denominator) || abs(denominator) <= 1.0e-8)
	{
		return 0.0;
	}

	return max(numerator / denominator, 0.0);
}
