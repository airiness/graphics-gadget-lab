#include <Common/ApplicationBinding.hlsli>
#include <Common/BindlessResources.hlsli>
#include <Common/TemporalAA.hlsli>
#include <Common/Common.hlsli>

struct TemporalAAPassParameters
{
	uint CurrentColorIndex;
	uint MotionIndex;
	uint CurrentDepthIndex;
	uint PreviousColorIndex;
	uint PreviousDepthIndex;
	uint ResolvedColorUavIndex;
	uint NextHistoryColorUavIndex;
	uint ReprojectionDiagnosticsUavIndex;
	// Linear clamp sampler in the low 16 bits, point clamp in the high 16 bits.
	uint PackedClampSamplerIndices;
	float VarianceClipGamma;
	uint ViewIndexAndHistoryValid;
	uint PackedDepthThresholds;
	uint PackedMaxHistoryFeedbackAndClampExpansion;
	float VelocityWeightScale;
	float LuminanceWeightScale;
	// Display-extent depth for post-temporal composition, written when the render
	// extent is smaller than the display extent.
	uint DisplayDepthUavIndex;
};

ConstantBuffer<TemporalAAPassParameters> g_Pass : register(b2);

[numthreads(8, 8, 1)]
void CSMain(uint3 dispatchThreadId : SV_DispatchThreadID)
{
	// The resolve writes display pixels from render-domain color, depth and motion. At
	// native resolution the two extents coincide and every render position is exact.
	RWTexture2D<float4> resolvedColor =
		GetRWTexture2DFloat4(g_Pass.ResolvedColorUavIndex);
	uint width;
	uint height;
	resolvedColor.GetDimensions(width, height);
	const uint2 pixel = dispatchThreadId.xy;
	if (any(pixel >= uint2(width, height)))
	{
		return;
	}
	Texture2D<float4> currentColorTexture = GetTexture2DFloat4(g_Pass.CurrentColorIndex);
	uint renderWidth;
	uint renderHeight;
	currentColorTexture.GetDimensions(renderWidth, renderHeight);
	const uint2 renderExtent = uint2(renderWidth, renderHeight);
	const float2 renderPosition =
		(float2(pixel) + 0.5.xx) * (float2(renderExtent) / float2(width, height));
	const uint2 renderPixel = min(uint2(renderPosition), renderExtent - 1u);
	const float2 outputOffset = renderPosition - (float2(renderPixel) + 0.5.xx);

	Texture2D<float2> motionTexture = GetTexture2DFloat2(g_Pass.MotionIndex);
	Texture2D<float> currentDepthTexture = GetTexture2DFloat(g_Pass.CurrentDepthIndex);
	RWTexture2D<float4> nextHistoryColor =
		GetRWTexture2DFloat4(g_Pass.NextHistoryColorUavIndex);
	RWTexture2D<float4> reprojectionDiagnostics =
		GetRWTexture2DFloat4(g_Pass.ReprojectionDiagnosticsUavIndex);

	const float2 currentUV = (float2(pixel) + 0.5.xx) / float2(width, height);
	const float currentRawDepth = currentDepthTexture.Load(int3(renderPixel, 0));
	const uint viewIndex = g_Pass.ViewIndexAndHistoryValid & ~TAA_VIEW_FLAG_MASK;
	const bool previousHistoryValid =
		(g_Pass.ViewIndexAndHistoryValid & TAA_HISTORY_VALID_BIT) != 0;
	const bool writeHistoryColorPreview =
		(g_Pass.ViewIndexAndHistoryValid & TAA_HISTORY_COLOR_PREVIEW_BIT) != 0;
	const bool writeHistorySamplesPreview =
		(g_Pass.ViewIndexAndHistoryValid & TAA_HISTORY_SAMPLES_PREVIEW_BIT) != 0;
	const bool writeClipDistancePreview =
		(g_Pass.ViewIndexAndHistoryValid & TAA_CLIP_DISTANCE_PREVIEW_BIT) != 0;
	const bool catmullRomHistory =
		(g_Pass.ViewIndexAndHistoryValid & TAA_HISTORY_CATMULL_ROM_BIT) != 0;
	const bool gaussianCurrent =
		(g_Pass.ViewIndexAndHistoryValid & TAA_CURRENT_GAUSSIAN_BIT) != 0;
	const bool closestDepthMotion =
		(g_Pass.ViewIndexAndHistoryValid & TAA_CLOSEST_DEPTH_MOTION_BIT) != 0;
	const bool writeDisplayDepth =
		(g_Pass.ViewIndexAndHistoryValid & TAA_DISPLAY_DEPTH_BIT) != 0;
	const bool effectiveSamples =
		(g_Pass.ViewIndexAndHistoryValid & TAA_EFFECTIVE_SAMPLES_BIT) != 0;
	const ViewData viewData = g_Views[g_Scene.ViewBaseIndex + viewIndex];
	const float2 depthThresholds =
		UnpackTemporalAAUnitRangePair(g_Pass.PackedDepthThresholds);
	const float2 maxHistoryFeedbackAndClampExpansion =
		UnpackTemporalAAUnitRangePair(g_Pass.PackedMaxHistoryFeedbackAndClampExpansion);
	float3 centerColor = currentColorTexture.Load(int3(renderPixel, 0)).rgb;
	if (!IsTemporalColorFinite(centerColor))
	{
		centerColor = 0.0.xxx;
	}
	float3 currentColor = centerColor;
	if (gaussianCurrent)
	{
		currentColor = ReconstructTemporalCurrentColor(currentColorTexture, renderPixel,
			renderExtent, viewData.CurrentJitterUV * float2(renderExtent), outputOffset,
			TAA_CURRENT_GAUSSIAN_KERNEL_SCALE, centerColor);
	}

	uint rejectionReason = TAA_REJECTION_HISTORY_UNAVAILABLE;
	float2 previousHistoryUV = currentUV;
	float2 previousRasterUV = currentUV;
	float2 historyMotionUV = 0.0.xx;
	bool accepted = false;
	float3 historyColor = currentColor;
	float previousAccumulation = TAA_HISTORY_INITIAL_ACCUMULATION;
	if (previousHistoryValid)
	{
		// The sample whose motion reprojects this pixel: the centre, or the front-most
		// depth of the 3x3 neighborhood. Background is never nearer than geometry.
		int2 correspondencePixel = int2(renderPixel);
		float correspondenceDepth = currentRawDepth;
		if (closestDepthMotion)
		{
			const int2 maxPixel = int2(renderExtent) - 1;
			[unroll]
			for (int y = -1; y <= 1; ++y)
			{
				[unroll]
				for (int x = -1; x <= 1; ++x)
				{
					const int2 samplePixel =
						clamp(int2(renderPixel) + int2(x, y), 0, maxPixel);
					const float sampleDepth = currentDepthTexture.Load(int3(samplePixel, 0));
					if (isfinite(sampleDepth) && IsDepthNearer(
						sampleDepth, correspondenceDepth, viewData.DepthConvention))
					{
						correspondencePixel = samplePixel;
						correspondenceDepth = sampleDepth;
					}
				}
			}
		}
		if (IsDepthBackground(correspondenceDepth, viewData.DepthConvention))
		{
			previousRasterUV = ReprojectTemporalSkyUV(currentUV, viewData);
		}
		else
		{
			const float2 motionUV = motionTexture.Load(int3(correspondencePixel, 0));
			previousRasterUV = ReprojectTemporalUV(currentUV, motionUV);
		}
		const float2 rasterMotionUV = currentUV - previousRasterUV;
		historyMotionUV = ResolveTemporalHistoryMotionUV(rasterMotionUV,
			viewData.CurrentJitterUV, viewData.PreviousJitterUV);
		previousHistoryUV = ReprojectTemporalUV(currentUV, historyMotionUV);
		// Depth validation tests the selected sample at its own position, so motion
		// and validation describe one surface.
		float2 validationUV = currentUV;
		float validationDepth = currentRawDepth;
		float2 validationPreviousRasterUV = previousRasterUV;
		if (closestDepthMotion)
		{
			validationUV = (float2(correspondencePixel) + 0.5.xx) / float2(renderExtent);
			validationDepth = correspondenceDepth;
			validationPreviousRasterUV = ReprojectTemporalUV(validationUV, rasterMotionUV);
		}

		if (!AreTemporalReprojectionUVsValid(previousHistoryUV, validationPreviousRasterUV))
		{
			rejectionReason = all(isfinite(previousHistoryUV)) &&
				all(isfinite(validationPreviousRasterUV))
				? TAA_REJECTION_PREVIOUS_UV_OUT_OF_BOUNDS
				: TAA_REJECTION_NON_FINITE;
		}
		else
		{
			Texture2D<float4> previousColorTexture =
				GetTexture2DFloat4(g_Pass.PreviousColorIndex);
			SamplerState linearClampSampler =
				GetSamplerState(g_Pass.PackedClampSamplerIndices & 0xffffu);
			SamplerState pointClampSampler =
				GetSamplerState(g_Pass.PackedClampSamplerIndices >> 16);
			historyColor = catmullRomHistory
				? SampleTemporalHistoryCatmullRomClamped(previousColorTexture,
					linearClampSampler, previousHistoryUV)
				: previousColorTexture.SampleLevel(
					linearClampSampler, previousHistoryUV, 0.0).rgb;
			historyColor = float3(
				RescaleHistoryColorChannel(historyColor.r, viewData.ScenePreExposure, viewData.PreviousScenePreExposure),
				RescaleHistoryColorChannel(historyColor.g, viewData.ScenePreExposure, viewData.PreviousScenePreExposure),
				RescaleHistoryColorChannel(historyColor.b, viewData.ScenePreExposure, viewData.PreviousScenePreExposure));
			// The accumulation state of the nearest texel. Its minimum over the bilinear
			// footprint spread intermittent alpha-test rejections to the neighboring
			// pixels and raised static shimmer.
			previousAccumulation = previousColorTexture.SampleLevel(
				pointClampSampler, previousHistoryUV, 0.0).a;
			if (!IsTemporalColorFinite(historyColor) ||
				!IsTemporalHistoryAccumulationValid(previousAccumulation))
			{
				rejectionReason = TAA_REJECTION_NON_FINITE;
			}
			else if (IsDepthBackground(validationDepth, viewData.DepthConvention))
			{
				Texture2D<float> previousDepthTexture =
					GetTexture2DFloat(g_Pass.PreviousDepthIndex);
				const float previousRawDepth = previousDepthTexture.SampleLevel(
					pointClampSampler, validationPreviousRasterUV, 0.0);
				if (!isfinite(previousRawDepth))
				{
					rejectionReason = TAA_REJECTION_NON_FINITE;
				}
				else
				{
					accepted = IsDepthBackground(
						previousRawDepth, viewData.PreviousDepthConvention);
					rejectionReason = accepted
						? TAA_REJECTION_NONE
						: TAA_REJECTION_BACKGROUND_MISMATCH;
				}
			}
			else
			{
				Texture2D<float> previousDepthTexture =
					GetTexture2DFloat(g_Pass.PreviousDepthIndex);
				accepted = ValidateTemporalGeometryDepth(validationUV, validationDepth,
					validationPreviousRasterUV, previousDepthTexture, pointClampSampler,
					viewData, depthThresholds.x, depthThresholds.y);
				rejectionReason = accepted ? TAA_REJECTION_NONE : TAA_REJECTION_DEPTH_MISMATCH;
			}
		}
	}

	float historyWeight = 0.0;
	float historyConfidence = 0.0;
	// How far rectification moved accepted history, relative to the size of the
	// neighborhood box; history that is not accepted counts as fully discarded.
	float clipDistance = 1.0;
	if (accepted)
	{
		float3 neighborhoodMin;
		float3 neighborhoodMax;
		float3 neighborhoodMean;
		float3 neighborhoodStdDev;
		GetTemporalNeighborhoodRange(currentColorTexture, renderPixel, renderExtent,
			centerColor, neighborhoodMin, neighborhoodMax, neighborhoodMean, neighborhoodStdDev);
		const float clampExpansion = maxHistoryFeedbackAndClampExpansion.y;
		const float3 neighborhoodExtent = neighborhoodMax - neighborhoodMin;
		neighborhoodMin -= neighborhoodExtent * clampExpansion;
		neighborhoodMax += neighborhoodExtent * clampExpansion;

		const float3 currentYCoCg = TemporalRGBToYCoCg(currentColor);
		const float3 historyYCoCg = TemporalRGBToYCoCg(historyColor);
		const float motionMagnitudePixels =
			length(historyMotionUV * float2(width, height));
		historyConfidence = ComputeTemporalHistoryConfidence(motionMagnitudePixels,
			currentYCoCg.x * ExposureScaleOverPreExposure(viewData.ExposureMultiplier, viewData.ScenePreExposure),
			historyYCoCg.x * ExposureScaleOverPreExposure(viewData.ExposureMultiplier, viewData.ScenePreExposure),
			g_Pass.VelocityWeightScale, g_Pass.LuminanceWeightScale);
		historyWeight = ComputeTemporalHistoryWeight(previousAccumulation, historyConfidence,
			maxHistoryFeedbackAndClampExpansion.x);
		const bool varianceClip =
			(g_Pass.ViewIndexAndHistoryValid & TAA_VARIANCE_CLIP_BIT) != 0;
		float3 rectifiedYCoCg;
		if (varianceClip)
		{
			const float3 varianceExtent = g_Pass.VarianceClipGamma * neighborhoodStdDev;
			float3 boxMin = neighborhoodMean - varianceExtent;
			float3 boxMax = neighborhoodMean + varianceExtent;
			if ((g_Pass.ViewIndexAndHistoryValid & TAA_VARIANCE_CLIP_BOUNDED_BIT) != 0)
			{
				boxMin = max(boxMin, neighborhoodMin);
				boxMax = min(boxMax, neighborhoodMax);
			}
			rectifiedYCoCg =
				ClipTemporalHistoryTowardMean(historyYCoCg, neighborhoodMean, boxMin, boxMax);
		}
		else
		{
			rectifiedYCoCg = clamp(historyYCoCg, neighborhoodMin, neighborhoodMax);
		}
		historyColor = TemporalYCoCgToRGB(rectifiedYCoCg);
		clipDistance = saturate(length(rectifiedYCoCg - historyYCoCg) /
			max(length(neighborhoodMax - neighborhoodMin), 1.0e-6));
		if (!IsTemporalColorFinite(historyColor) || !isfinite(historyWeight))
		{
			historyColor = currentColor;
			historyWeight = 0.0;
			accepted = false;
			rejectionReason = TAA_REJECTION_NON_FINITE;
		}
	}

	float3 outputColor = lerp(currentColor, historyColor, historyWeight);
	if (!IsTemporalColorFinite(outputColor))
	{
		outputColor = currentColor;
		accepted = false;
		rejectionReason = TAA_REJECTION_NON_FINITE;
	}

	const float maxHistorySamples =
		ResolveTemporalAAMaxHistorySamples(maxHistoryFeedbackAndClampExpansion.x);
	const float nextAccumulation = effectiveSamples
		? ResolveTemporalHistoryNextSamples(accepted, previousAccumulation,
			historyConfidence, maxHistorySamples)
		: ResolveTemporalHistoryNextAge(accepted, previousAccumulation);
	const float2 outputAlphas = ResolveTemporalAAOutputAlphas(nextAccumulation);
	const float4 resolvedOutput = float4(SanitizeHDRColor(outputColor), outputAlphas.x);
	// Half precision stores a compatibility age exactly and an effective sample count
	// within a relative 2^-11.
	const float4 historyOutput = float4(QuantizeTemporalHistoryColor(
		resolvedOutput.rgb, pixel, viewData.TemporalFrameIndex), outputAlphas.y);
	resolvedColor[pixel] = resolvedOutput;
	nextHistoryColor[pixel] = historyOutput;
	float4 diagnosticsOutput =
		float4(historyWeight, float(rejectionReason), previousHistoryUV);
	if (writeHistoryColorPreview)
	{
		diagnosticsOutput = resolvedOutput;
	}
	else if (writeHistorySamplesPreview)
	{
		const float normalizedAccumulation = effectiveSamples
			? ResolveTemporalAAHistorySamplesPreview(nextAccumulation, maxHistorySamples)
			: ResolveTemporalAAHistoryAgePreview(
				nextAccumulation, maxHistoryFeedbackAndClampExpansion.x);
		diagnosticsOutput = float4(normalizedAccumulation.xxx, 1.0);
	}
	else if (writeClipDistancePreview)
	{
		diagnosticsOutput = float4((accepted ? clipDistance : 1.0).xxx, 1.0);
	}
	reprojectionDiagnostics[pixel] = diagnosticsOutput;

	if (writeDisplayDepth)
	{
		// The render sample whose jittered position is nearest the display pixel centre.
		const float2 jitterPixels = viewData.CurrentJitterUV * float2(renderExtent);
		const uint2 depthPixel = min(uint2(max(renderPosition + jitterPixels, 0.0.xx)),
			renderExtent - 1u);
		RWTexture2D<float> displayDepth = GetRWTexture2DFloat(g_Pass.DisplayDepthUavIndex);
		displayDepth[pixel] = currentDepthTexture.Load(int3(depthPixel, 0));
	}
}
