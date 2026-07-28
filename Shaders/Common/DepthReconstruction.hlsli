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

bool IsDepthBackground(float rawDepth, uint convention, float tolerance)
{
	tolerance = max(tolerance, 0.0);
	return convention == DEPTH_CONVENTION_REVERSED ?
		rawDepth <= tolerance :
		rawDepth >= 1.0 - tolerance;
}

bool IsDepthNearer(float lhs, float rhs, uint convention)
{
	return convention == DEPTH_CONVENTION_REVERSED ? lhs > rhs : lhs < rhs;
}

bool IsDepthFarther(float lhs, float rhs, uint convention)
{
	return convention == DEPTH_CONVENTION_REVERSED ? lhs < rhs : lhs > rhs;
}

float3 ReconstructViewPosition(
	float2 uv,
	float rawDepth,
	float4x4 inverseProjection)
{
	return ReconstructPositionFromRawDepth(uv, rawDepth, inverseProjection);
}

float3 ReconstructWorldPosition(
	float2 uv,
	float rawDepth,
	float4x4 inverseViewProjection)
{
	return ReconstructPositionFromRawDepth(uv, rawDepth, inverseViewProjection);
}

float RawDepthToPositiveViewZ(
	float2 uv,
	float rawDepth,
	float4x4 inverseProjection)
{
	return max(ReconstructViewPosition(uv, rawDepth, inverseProjection).z, 0.0);
}
