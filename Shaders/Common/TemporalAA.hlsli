#pragma once

#include <Common/DepthReconstruction.hlsli>
#include <Common/Temporal.hlsli>
#include <Common/TemporalMotion.hlsli>

static const uint TAA_REJECTION_NONE = 0;
static const uint TAA_REJECTION_HISTORY_UNAVAILABLE = 1;
static const uint TAA_REJECTION_PREVIOUS_UV_OUT_OF_BOUNDS = 2;
static const uint TAA_REJECTION_NON_FINITE = 3;
static const uint TAA_REJECTION_DEPTH_MISMATCH = 4;
static const uint TAA_REJECTION_BACKGROUND_MISMATCH = 5;

bool IsTemporalUVInBounds(float2 uv)
{
	return all(isfinite(uv)) && all(uv >= 0.0.xx) && all(uv <= 1.0.xx);
}

bool IsTemporalColorFinite(float3 color)
{
	return all(isfinite(color));
}

bool IsTemporalDepthCompatible(float expectedPreviousViewZ, float storedPreviousViewZ,
	float absoluteThreshold, float relativeThreshold)
{
	if (!isfinite(expectedPreviousViewZ) || !isfinite(storedPreviousViewZ) ||
		expectedPreviousViewZ <= 0.0 || storedPreviousViewZ <= 0.0 ||
		!isfinite(absoluteThreshold) || !isfinite(relativeThreshold) ||
		absoluteThreshold < 0.0 || relativeThreshold < 0.0)
	{
		return false;
	}

	const float tolerance = max(absoluteThreshold,
		relativeThreshold * expectedPreviousViewZ);
	return abs(expectedPreviousViewZ - storedPreviousViewZ) <= tolerance;
}

float2 ReprojectTemporalSkyUV(float2 currentUV, ViewData viewData)
{
	const float currentFarDepth = GetDepthFarValue(viewData.DepthConvention);
	const float3 currentDirectionVS = ReconstructViewPosition(
		currentUV, currentFarDepth, viewData.InvProjMat);
	const float3 currentDirectionWS =
		mul(float4(currentDirectionVS, 0.0), viewData.InvViewMat).xyz;
	const float4 previousClip =
		mul(float4(currentDirectionWS, 0.0), viewData.PreviousRasterViewProj);
	if (!all(isfinite(previousClip)) || abs(previousClip.w) <= 1.0e-8)
	{
		return asfloat(uint2(0x7fc00000, 0x7fc00000));
	}
	return TemporalClipPositionToUV(previousClip);
}

bool ValidateTemporalGeometryDepth(float2 currentUV, float currentRawDepth,
	float2 previousUV, Texture2D<float> previousDepthTexture, SamplerState pointClampSampler,
	ViewData viewData, float absoluteThreshold, float relativeThreshold)
{
	const float3 currentPositionVS = ReconstructViewPosition(
		currentUV, currentRawDepth, viewData.InvProjMat);
	const float3 currentPositionWS =
		mul(float4(currentPositionVS, 1.0), viewData.InvViewMat).xyz;
	const float expectedPreviousViewZ =
		mul(float4(currentPositionWS, 1.0), viewData.PreviousViewMat).z;
	const float previousRawDepth =
		previousDepthTexture.SampleLevel(pointClampSampler, previousUV, 0.0);
	const float storedPreviousViewZ = RawDepthToPositiveViewZ(previousRawDepth,
		viewData.PreviousDepthReconstructionParams, viewData.PreviousDepthConvention);
	return IsTemporalDepthCompatible(expectedPreviousViewZ, storedPreviousViewZ,
		absoluteThreshold, relativeThreshold);
}
