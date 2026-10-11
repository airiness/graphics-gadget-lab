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
static const uint TAA_HISTORY_SAMPLES_PREVIEW_BIT = 0x20000000u;
static const uint TAA_HISTORY_CATMULL_ROM_BIT = 0x10000000u;
static const uint TAA_CURRENT_GAUSSIAN_BIT = 0x08000000u;
static const uint TAA_CLOSEST_DEPTH_MOTION_BIT = 0x04000000u;
static const uint TAA_DISPLAY_DEPTH_BIT = 0x02000000u;
static const uint TAA_VARIANCE_CLIP_BIT = 0x01000000u;
static const uint TAA_VARIANCE_CLIP_BOUNDED_BIT = 0x00800000u;
static const uint TAA_CLIP_DISTANCE_PREVIEW_BIT = 0x00400000u;
static const uint TAA_EFFECTIVE_SAMPLES_BIT = 0x00200000u;
static const uint TAA_HISTORY_RELAXATION_PREVIEW_BIT = 0x00100000u;
static const uint TAA_VIEW_FLAG_MASK =
	TAA_HISTORY_VALID_BIT | TAA_HISTORY_COLOR_PREVIEW_BIT |
	TAA_HISTORY_SAMPLES_PREVIEW_BIT | TAA_HISTORY_CATMULL_ROM_BIT |
	TAA_CURRENT_GAUSSIAN_BIT | TAA_CLOSEST_DEPTH_MOTION_BIT | TAA_DISPLAY_DEPTH_BIT |
	TAA_VARIANCE_CLIP_BIT | TAA_VARIANCE_CLIP_BOUNDED_BIT | TAA_CLIP_DISTANCE_PREVIEW_BIT |
	TAA_EFFECTIVE_SAMPLES_BIT | TAA_HISTORY_RELAXATION_PREVIEW_BIT;
// exp(-2.29 (d / 0.75)^2): Blackman-Harris approximated by a Gaussian of 0.75 pixels.
static const float TAA_CURRENT_GAUSSIAN_KERNEL_SCALE = 2.29 / (0.75 * 0.75);
// History alpha holds a compatibility age or an effective sample count, by the history's
// accumulation model. Both start at one and stay within these bounds.
static const float TAA_HISTORY_INITIAL_ACCUMULATION = 1.0;
static const float TAA_HISTORY_MAX_ACCUMULATION = 255.0;
// Smoothing of the reliability evidence: one native jitter cycle; 1/16 measured the same.
static const float TAA_RELIABILITY_SMOOTHING = 0.125;
// Below the display extent, history motion in display pixels per frame over which the
// static clamp expansion falls to zero.
static const float TAA_UPSCALED_STATIC_MOTION_PIXELS = 0.5;
// Min/max box expansion, in box extents per side, added for static history below the
// display extent.
static const float TAA_UPSCALED_STATIC_CLAMP_EXPANSION = 1.0;
// Lower bound of the samples a current frame counts as, so history weights stay below one.
static const float TAA_UPSCALED_MIN_SAMPLE_WEIGHT = 1.0e-3;

float2 UnpackTemporalAAUnitRangePair(uint packedValues)
{
	return float2(packedValues & 0xffffu, packedValues >> 16u) / 65535.0;
}

bool IsTemporalColorFinite(float3 color)
{
	return all(isfinite(color));
}

bool IsTemporalHistoryAccumulationValid(float accumulation)
{
	return isfinite(accumulation) && accumulation >= TAA_HISTORY_INITIAL_ACCUMULATION &&
		accumulation <= TAA_HISTORY_MAX_ACCUMULATION;
}

// Relative luminance difference of the current frame from history, in [-1, 1].
float ComputeTemporalRelativeLuminanceDifference(float currentLuminance, float historyLuminance)
{
	const float current = max(currentLuminance, 0.0);
	const float history = max(historyLuminance, 0.0);
	return (current - history) / max(max(current, history), 1.0e-4);
}

// Reliability evidence of accepted history: the signed and the absolute relative
// luminance difference, exponentially smoothed. Rejected history starts again from zero.
float2 ResolveTemporalReliability(float2 previousReliability, float difference)
{
	const float2 carried = all(isfinite(previousReliability)) ? previousReliability : 0.0.xx;
	return lerp(carried, float2(difference, abs(difference)), TAA_RELIABILITY_SMOOTHING);
}

// How consistently the current frame departs from history in one direction, in [0, 1]:
// near zero while jitter aliasing alternates the sign of the difference, near one while a
// shading or lighting change keeps it.
float ResolveTemporalDisagreementConsistency(float2 reliability)
{
	return saturate(abs(reliability.x) / max(reliability.y, 1.0e-4));
}

// Expansion of the min/max box, in box extents per side, for history whose disagreement
// alternates in sign: all of it below a consistency of 0.2 and none above 0.5, scaled by how
// close the accumulated samples are to their bound. A single frame's clip distance cannot
// separate jitter aliasing from stale history; the sign of the disagreement over frames can.
float ResolveTemporalHistoryRelaxation(float historyRelaxation, float accumulation,
	float maxSamples, float consistency)
{
	const float accumulated = saturate((accumulation - TAA_HISTORY_INITIAL_ACCUMULATION) /
		max(maxSamples - TAA_HISTORY_INITIAL_ACCUMULATION, 1.0));
	return max(historyRelaxation, 0.0) * accumulated * saturate((0.5 - consistency) / 0.3);
}

// Compatibility age: consecutive accepted frames, whatever their confidence.
float ResolveTemporalHistoryNextAge(bool historyAccepted, float previousHistoryAge)
{
	return historyAccepted && IsTemporalHistoryAccumulationValid(previousHistoryAge)
		? min(previousHistoryAge + 1.0, TAA_HISTORY_MAX_ACCUMULATION)
		: TAA_HISTORY_INITIAL_ACCUMULATION;
}

// Sample bound of effective-sample accumulation: the count whose weight N / (N + 1)
// equals the feedback ceiling.
float ResolveTemporalAAMaxHistorySamples(float maxHistoryFeedback)
{
	const float feedback = clamp(maxHistoryFeedback, 0.0, 65534.0 / 65535.0);
	return clamp(feedback / (1.0 - feedback), TAA_HISTORY_INITIAL_ACCUMULATION,
		TAA_HISTORY_MAX_ACCUMULATION);
}

// Effective sample count: the history confidence discounts the carried samples and the
// current frame adds the samples it counts as (one at native resolution), so a
// low-confidence frame also lowers the weight of the frames after it until evidence
// accumulates again.
float ResolveTemporalHistoryNextSamples(bool historyAccepted, float previousSamples,
	float historyConfidence, float maxSamples, float currentSampleWeight)
{
	if (!historyAccepted || !IsTemporalHistoryAccumulationValid(previousSamples) ||
		!isfinite(historyConfidence))
	{
		return TAA_HISTORY_INITIAL_ACCUMULATION;
	}
	return clamp(saturate(historyConfidence) * min(previousSamples, maxSamples) +
		currentSampleWeight, TAA_HISTORY_INITIAL_ACCUMULATION, maxSamples);
}

// How static history is below the display extent: one at rest, zero from
// TAA_UPSCALED_STATIC_MOTION_PIXELS of motion.
float ResolveTemporalUpscaledStaticFraction(float motionMagnitudePixels)
{
	return isfinite(motionMagnitudePixels)
		? 1.0 - saturate(motionMagnitudePixels / TAA_UPSCALED_STATIC_MOTION_PIXELS)
		: 0.0;
}

// Min/max box expansion, in box extents per side, for static history below the display
// extent. Detail finer than the render grid can miss all nine render samples of a frame, and
// a box without it pulls accumulated detail toward that frame's aliasing. Missed detail can
// keep the sign of the disagreement for several frames, so the expansion withdraws only as
// the disagreement consistency approaches one, as it does through a gradual lighting change.
float ResolveTemporalUpscaledStaticClampExpansion(float motionMagnitudePixels,
	float consistency)
{
	return TAA_UPSCALED_STATIC_CLAMP_EXPANSION *
		ResolveTemporalUpscaledStaticFraction(motionMagnitudePixels) *
		saturate((0.95 - consistency) / 0.15);
}

// Kernel weight, in display pixels, of the jittered render sample nearest the output
// pixel centre. offsetPixels is the output position relative to the centre of its render
// pixel plus the jitter, in render pixels; the nearest sample is the integer offset nearest it.
float ResolveTemporalNearestSampleWeight(float2 offsetPixels, float2 displayPerRender,
	float kernelScale)
{
	const float2 distance = (round(offsetPixels) - offsetPixels) * displayPerRender;
	return exp(-kernelScale * dot(distance, distance));
}

// Samples the current frame counts as below the display extent. The jitter cycle places a
// render sample near each display pixel only in some frames, so each frame counts by the
// kernel weight of its nearest sample instead of as one sample, as at native resolution.
float ResolveTemporalUpscaledSampleWeight(float nearestSampleWeight)
{
	return max(saturate(nearestSampleWeight), TAA_UPSCALED_MIN_SAMPLE_WEIGHT);
}

// Catmull-Rom resampling of the history color with five bilinear fetches: the 4x4
// kernel's separable weights are folded into bilinear taps along the centre rows and
// columns, and the four corner taps (the smallest weights) are dropped and the rest
// renormalized. At a texel centre it returns that texel exactly. The negative lobes
// overshoot at edges, so the result is limited to the range of the 2x2 texels a
// bilinear fetch would blend; the caller's neighborhood rectification bounds it
// further. Measured against supersampled references, the unlimited kernel doubled the
// overshoot increase for a few percent less reference error.
float3 SampleTemporalHistoryCatmullRomClamped(Texture2D<float4> history,
	SamplerState linearClamp, float2 uv)
{
	uint width;
	uint height;
	history.GetDimensions(width, height);
	const float2 extent = float2(width, height);
	const float2 position = uv * extent;
	const float2 center1 = floor(position - 0.5) + 0.5;
	const float2 f = position - center1;
	const float2 w0 = f * (-0.5 + f * (1.0 - 0.5 * f));
	const float2 w1 = 1.0 + f * f * (-2.5 + 1.5 * f);
	const float2 w2 = f * (0.5 + f * (2.0 - 1.5 * f));
	const float2 w3 = f * f * (-0.5 + 0.5 * f);
	const float2 w12 = w1 + w2;
	const float2 uv0 = (center1 - 1.0) / extent;
	const float2 uv12 = (center1 + w2 / w12) / extent;
	const float2 uv3 = (center1 + 2.0) / extent;

	const float weightTop = w12.x * w0.y;
	const float weightLeft = w0.x * w12.y;
	const float weightCenter = w12.x * w12.y;
	const float weightRight = w3.x * w12.y;
	const float weightBottom = w12.x * w3.y;
	const float3 sum =
		history.SampleLevel(linearClamp, float2(uv12.x, uv0.y), 0.0).rgb * weightTop +
		history.SampleLevel(linearClamp, float2(uv0.x, uv12.y), 0.0).rgb * weightLeft +
		history.SampleLevel(linearClamp, uv12, 0.0).rgb * weightCenter +
		history.SampleLevel(linearClamp, float2(uv3.x, uv12.y), 0.0).rgb * weightRight +
		history.SampleLevel(linearClamp, float2(uv12.x, uv3.y), 0.0).rgb * weightBottom;
	const float weightSum =
		weightTop + weightLeft + weightCenter + weightRight + weightBottom;
	const int2 maxTexel = int2(width, height) - 1;
	const int2 texel = int2(center1 - 0.5);
	const float3 c00 = history.Load(int3(clamp(texel, 0, maxTexel), 0)).rgb;
	const float3 c10 = history.Load(int3(clamp(texel + int2(1, 0), 0, maxTexel), 0)).rgb;
	const float3 c01 = history.Load(int3(clamp(texel + int2(0, 1), 0, maxTexel), 0)).rgb;
	const float3 c11 = history.Load(int3(clamp(texel + int2(1, 1), 0, maxTexel), 0)).rgb;
	return clamp(sum / weightSum, min(min(c00, c10), min(c01, c11)),
		max(max(c00, c10), max(c01, c11)));
}

// Current color at the output pixel centre, reconstructed from the 3x3 neighborhood.
// The jitter shifts rendered geometry by +jitterPixels, so the sample of the
// neighbor at offset o lies at o - jitterPixels from the centre of the render pixel.
// outputOffset is the output position relative to that centre, in render pixels; zero
// when render and output pixels coincide. Each sample is weighted by
// exp(-kernelScale * distance^2) with the distance in output pixels, displayPerRender
// output pixels per render pixel, so the kernel keeps its width below the display extent;
// non-finite samples use centerColor.
float3 ReconstructTemporalCurrentColor(Texture2D<float4> currentColorTexture,
	uint2 pixel, uint2 extent, float2 jitterPixels, float2 outputOffset,
	float2 displayPerRender, float kernelScale, float3 centerColor)
{
	const int2 maxPixel = int2(extent) - 1;
	float3 sum = 0.0.xxx;
	float weightSum = 0.0;
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
				sampleColor = centerColor;
			}
			const float2 offset = (float2(x, y) - jitterPixels - outputOffset) * displayPerRender;
			const float weight = exp(-kernelScale * dot(offset, offset));
			sum += sampleColor * weight;
			weightSum += weight;
		}
	}
	return sum / weightSum;
}

// PCG3D (Jarzynski and Olano 2020): three decorrelated 32-bit hashes.
uint3 HashTemporalPcg3d(uint3 value)
{
	value = value * 1664525u + 1013904223u;
	value.x += value.y * value.z;
	value.y += value.z * value.x;
	value.z += value.x * value.y;
	value ^= value >> 16u;
	value.x += value.y * value.z;
	value.y += value.z * value.x;
	value.z += value.x * value.y;
	return value;
}

// Rounds a finite non-negative value to one of its two neighbouring half-precision
// values with a probability proportional to its distance from the other one, so the
// stored value is unbiased for any accumulation weight. Round-to-nearest storage
// discards every history update smaller than half a half-precision step; with
// asymmetric jittered samples the discarded updates do not cancel, and the
// accumulated color drifts away from the converged mean as the feedback grows.
// Does not depend on the rounding mode of f32tof16.
float RoundTemporalHistoryToHalfStochastic(float value, float uniformSample)
{
	const uint nearestBits = f32tof16(value);
	const float nearest = f16tof32(nearestBits);
	if (!(value > 0.0) || nearest == value)
	{
		return nearest;
	}
	// For positive half values the adjacent larger and smaller encodings are +1/-1.
	const bool roundedDown = nearest < value;
	const float adjacent = f16tof32(roundedDown ? nearestBits + 1u : nearestBits - 1u);
	const float lower = roundedDown ? nearest : adjacent;
	const float upper = roundedDown ? adjacent : nearest;
	const float upperProbability = (value - lower) / (upper - lower);
	return uniformSample < upperProbability ? upper : lower;
}

// History color as stored in its half-precision target, stochastically rounded with
// noise that varies per pixel, channel and temporal frame.
float3 QuantizeTemporalHistoryColor(float3 color, uint2 pixel, uint temporalFrameIndex)
{
	const uint3 hash = HashTemporalPcg3d(uint3(pixel, temporalFrameIndex));
	const float3 uniformSamples = float3(hash >> 8u) * (1.0 / 16777216.0);
	return float3(
		RoundTemporalHistoryToHalfStochastic(color.r, uniformSamples.x),
		RoundTemporalHistoryToHalfStochastic(color.g, uniformSamples.y),
		RoundTemporalHistoryToHalfStochastic(color.b, uniformSamples.z));
}

float2 ResolveTemporalAAOutputAlphas(float nextAccumulation)
{
	return float2(1.0, IsTemporalHistoryAccumulationValid(nextAccumulation)
		? nextAccumulation
		: TAA_HISTORY_INITIAL_ACCUMULATION);
}

float ResolveTemporalAAFeedbackSaturationAge(float maxHistoryFeedback)
{
	if (!isfinite(maxHistoryFeedback))
	{
		return TAA_HISTORY_INITIAL_ACCUMULATION;
	}

	const float maxPackedFeedback = 65534.0 / 65535.0;
	const float feedback = clamp(maxHistoryFeedback, 0.0, maxPackedFeedback);
	return max(ceil(feedback / max(1.0 - feedback, 1.0 / 65535.0)),
		TAA_HISTORY_INITIAL_ACCUMULATION);
}

float ResolveTemporalAAHistoryAgePreview(float nextHistoryAge,
	float maxHistoryFeedback)
{
	if (!IsTemporalHistoryAccumulationValid(nextHistoryAge))
	{
		return 0.0;
	}

	const float feedbackSaturationAge =
		ResolveTemporalAAFeedbackSaturationAge(maxHistoryFeedback);
	// Feedback uses PreviousAge while this preview displays stored NextAge.
	// Keep the saturation-age denominator intact; there is intentionally no -1.
	return saturate((nextHistoryAge - TAA_HISTORY_INITIAL_ACCUMULATION) /
		max(feedbackSaturationAge, TAA_HISTORY_INITIAL_ACCUMULATION));
}

// Stored effective samples from a reset (black) to the sample bound (white).
float ResolveTemporalAAHistorySamplesPreview(float nextSamples, float maxSamples)
{
	if (!IsTemporalHistoryAccumulationValid(nextSamples))
	{
		return 0.0;
	}
	return saturate((nextSamples - TAA_HISTORY_INITIAL_ACCUMULATION) /
		max(maxSamples - TAA_HISTORY_INITIAL_ACCUMULATION, 1.0e-6));
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

// Minimum, maximum, mean and standard deviation of the 3x3 YCoCg neighborhood.
void GetTemporalNeighborhoodRange(Texture2D<float4> currentColorTexture,
	uint2 pixel, uint2 extent, float3 fallbackColor,
	out float3 neighborhoodMin, out float3 neighborhoodMax,
	out float3 neighborhoodMean, out float3 neighborhoodStdDev)
{
	neighborhoodMin = float3(3.402823466e+38, 3.402823466e+38, 3.402823466e+38);
	neighborhoodMax = -neighborhoodMin;
	float3 sum = 0.0.xxx;
	float3 sumOfSquares = 0.0.xxx;
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
			sum += sampleYCoCg;
			sumOfSquares += sampleYCoCg * sampleYCoCg;
		}
	}
	neighborhoodMean = sum / 9.0;
	neighborhoodStdDev =
		sqrt(max(sumOfSquares / 9.0 - neighborhoodMean * neighborhoodMean, 0.0.xxx));
}

// Moves history along the line toward the neighborhood mean until it lies inside
// [boxMin, boxMax], which contains the mean. Returns the clipped history.
float3 ClipTemporalHistoryTowardMean(float3 history, float3 mean, float3 boxMin, float3 boxMax)
{
	const float3 direction = history - mean;
	const float3 limit = lerp(mean - boxMin, boxMax - mean, step(0.0, direction));
	const float3 axisScale = max(limit, 0.0.xxx) / max(abs(direction), 1.0e-7.xxx);
	const float scale = saturate(min(axisScale.x, min(axisScale.y, axisScale.z)));
	return mean + direction * scale;
}

// Confidence in accepted history, in [0, 1], from its motion and luminance change.
float ComputeTemporalHistoryConfidence(float motionMagnitudePixels,
	float currentLuminance, float historyLuminance,
	float velocityWeightScale, float luminanceWeightScale)
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
	return velocityConfidence * luminanceConfidence;
}

// Weight of accepted effective-sample history against a current frame that counts as
// currentSampleWeight samples: confidence * N / (N + w). With w = 1 it equals the weight of
// ComputeTemporalHistoryWeight, since the sample bound keeps N / (N + 1) at the feedback
// ceiling.
float ComputeTemporalWeightedHistoryWeight(float previousSamples, float historyConfidence,
	float maxSamples, float currentSampleWeight)
{
	if (!IsTemporalHistoryAccumulationValid(previousSamples) || !isfinite(historyConfidence))
	{
		return 0.0;
	}
	const float samples = min(previousSamples, maxSamples);
	return saturate(historyConfidence) * samples /
		(samples + max(currentSampleWeight, TAA_UPSCALED_MIN_SAMPLE_WEIGHT));
}

// Weight of accepted history, min(N / (N + 1), feedback) * confidence, for the stored
// accumulation N of either model.
float ComputeTemporalHistoryWeight(float previousAccumulation, float historyConfidence,
	float maxHistoryFeedback)
{
	if (!IsTemporalHistoryAccumulationValid(previousAccumulation) ||
		!isfinite(historyConfidence))
	{
		return 0.0;
	}

	const float accumulationWeight = previousAccumulation / (previousAccumulation + 1.0);
	return min(accumulationWeight, saturate(maxHistoryFeedback)) *
		saturate(historyConfidence);
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
	const float expectedPreviousViewZ = ResolveExpectedPreviousViewZ(
		currentPositionVS, viewData.InvViewMat, viewData.PreviousViewMat);

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
