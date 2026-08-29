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
static const uint TAA_HISTORY_VALID_BIT = 0x80000000u;
static const uint TAA_HISTORY_COLOR_PREVIEW_BIT = 0x40000000u;
static const uint TAA_VIEW_FLAG_MASK =
	TAA_HISTORY_VALID_BIT | TAA_HISTORY_COLOR_PREVIEW_BIT;
static const float TAA_HISTORY_INITIAL_AGE = 1.0;
static const float TAA_HISTORY_MAX_AGE = 255.0;

float2 UnpackTemporalAAUnitRangePair(uint packedValues)
{
	return float2(packedValues & 0xffffu, packedValues >> 16u) / 65535.0;
}

bool IsTemporalUVInBounds(float2 uv)
{
	return all(isfinite(uv)) && all(uv >= 0.0.xx) && all(uv <= 1.0.xx);
}

bool IsTemporalColorFinite(float3 color)
{
	return all(isfinite(color));
}

bool IsTemporalHistoryAgeValid(float historyAge)
{
	return isfinite(historyAge) && historyAge >= TAA_HISTORY_INITIAL_AGE &&
		historyAge <= TAA_HISTORY_MAX_AGE;
}

float ResolveTemporalHistoryNextAge(bool historyAccepted, float previousHistoryAge)
{
	return historyAccepted && IsTemporalHistoryAgeValid(previousHistoryAge)
		? min(previousHistoryAge + 1.0, TAA_HISTORY_MAX_AGE)
		: TAA_HISTORY_INITIAL_AGE;
}

float2 ResolveTemporalHistoryMotionUV(float2 rasterMotionUV,
	float2 currentJitterUV, float2 previousJitterUV)
{
	return rasterMotionUV - (currentJitterUV - previousJitterUV);
}

bool AreTemporalReprojectionUVsValid(float2 previousHistoryUV, float2 previousRasterUV)
{
	return IsTemporalUVInBounds(previousHistoryUV) &&
		IsTemporalUVInBounds(previousRasterUV);
}

float3 TemporalRGBToYCoCg(float3 color)
{
	return float3(
		dot(color, float3(0.25, 0.5, 0.25)),
		0.5 * color.r - 0.5 * color.b,
		-0.25 * color.r + 0.5 * color.g - 0.25 * color.b);
}

float3 TemporalYCoCgToRGB(float3 color)
{
	return float3(
		color.x + color.y - color.z,
		color.x + color.z,
		color.x - color.y - color.z);
}

void GetTemporalNeighborhoodRange(Texture2D<float4> currentColorTexture,
	uint2 pixel, uint2 extent, float3 fallbackColor,
	out float3 neighborhoodMin, out float3 neighborhoodMax)
{
	neighborhoodMin = float3(3.402823466e+38, 3.402823466e+38, 3.402823466e+38);
	neighborhoodMax = -neighborhoodMin;
	const int2 maxPixel = int2(extent) - 1;
	[unroll]
	for (int y = -1; y <= 1; ++y)
	{
		[unroll]
		for (int x = -1; x <= 1; ++x)
		{
			const int2 samplePixel = clamp(int2(pixel) + int2(x, y), 0, maxPixel);
			float3 sampleColor = currentColorTexture.Load(int3(samplePixel, 0)).rgb;
			if (!IsTemporalColorFinite(sampleColor))
			{
				sampleColor = fallbackColor;
			}
			const float3 sampleYCoCg = TemporalRGBToYCoCg(sampleColor);
			neighborhoodMin = min(neighborhoodMin, sampleYCoCg);
			neighborhoodMax = max(neighborhoodMax, sampleYCoCg);
		}
	}
}

float ComputeTemporalHistoryWeight(float motionMagnitudePixels,
	float currentLuminance, float historyLuminance,
	float historyWeight, float velocityWeightScale, float luminanceWeightScale)
{
	if (!isfinite(motionMagnitudePixels) || motionMagnitudePixels < 0.0 ||
		!isfinite(currentLuminance) || !isfinite(historyLuminance))
	{
		return 0.0;
	}

	const float velocityConfidence =
		1.0 - saturate(motionMagnitudePixels * max(velocityWeightScale, 0.0));
	const float luminanceDenominator = max(
		max(abs(currentLuminance), abs(historyLuminance)), 1.0e-4);
	const float relativeLuminanceDifference =
		abs(currentLuminance - historyLuminance) / luminanceDenominator;
	const float luminanceConfidence =
		1.0 - saturate(relativeLuminanceDifference * max(luminanceWeightScale, 0.0));
	return saturate(historyWeight) * velocityConfidence * luminanceConfidence;
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
	float2 previousRasterUV, Texture2D<float> previousDepthTexture,
	SamplerState pointClampSampler,
	ViewData viewData, float absoluteThreshold, float relativeThreshold)
{
	const float3 currentPositionVS = ReconstructViewPosition(
		currentUV, currentRawDepth, viewData.InvProjMat);
	const float3 currentPositionWS =
		mul(float4(currentPositionVS, 1.0), viewData.InvViewMat).xyz;
	const float expectedPreviousViewZ =
		mul(float4(currentPositionWS, 1.0), viewData.PreviousViewMat).z;

	uint previousDepthWidth;
	uint previousDepthHeight;
	previousDepthTexture.GetDimensions(previousDepthWidth, previousDepthHeight);
	const float2 previousDepthTexelSize =
		rcp(float2(max(previousDepthWidth, 1u), max(previousDepthHeight, 1u)));

	[unroll]
	for (int y = -1; y <= 1; ++y)
	{
		[unroll]
		for (int x = -1; x <= 1; ++x)
		{
			const float2 sampleUV =
				previousRasterUV + float2(x, y) * previousDepthTexelSize;
			const float previousRawDepth =
				previousDepthTexture.SampleLevel(pointClampSampler, sampleUV, 0.0);
			if (!isfinite(previousRawDepth) || IsDepthBackground(
				previousRawDepth, viewData.PreviousDepthConvention))
			{
				continue;
			}

			const float storedPreviousViewZ = RawDepthToPositiveViewZ(previousRawDepth,
				viewData.PreviousDepthReconstructionParams,
				viewData.PreviousDepthConvention);
			if (IsTemporalDepthCompatible(expectedPreviousViewZ, storedPreviousViewZ,
				absoluteThreshold, relativeThreshold))
			{
				return true;
			}
		}
	}

	return false;
}
