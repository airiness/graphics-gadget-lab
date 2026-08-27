#include <Common/ApplicationBinding.hlsli>
#include <Common/BindlessResources.hlsli>
#include <Common/TemporalAA.hlsli>

struct TemporalAAPassParameters
{
	uint CurrentColorIndex;
	uint MotionIndex;
	uint CurrentDepthIndex;
	uint PreviousColorIndex;
	uint PreviousDepthIndex;
	uint ResolvedColorUavIndex;
	uint NextHistoryColorUavIndex;
	uint NextHistoryDepthUavIndex;
	uint ReprojectionDiagnosticsUavIndex;
	uint LinearClampSamplerIndex;
	uint PointClampSamplerIndex;
	uint ViewIndex;
	uint PreviousHistoryValid;
	float DepthAbsoluteThreshold;
	float DepthRelativeThreshold;
	float RestrictedHistoryWeight;
};

ConstantBuffer<TemporalAAPassParameters> g_Pass : register(b2);

[numthreads(8, 8, 1)]
void CSMain(uint3 dispatchThreadId : SV_DispatchThreadID)
{
	Texture2D<float4> currentColorTexture = GetTexture2DFloat4(g_Pass.CurrentColorIndex);
	uint width;
	uint height;
	currentColorTexture.GetDimensions(width, height);
	const uint2 pixel = dispatchThreadId.xy;
	if (any(pixel >= uint2(width, height)))
	{
		return;
	}

	Texture2D<float2> motionTexture = GetTexture2DFloat2(g_Pass.MotionIndex);
	Texture2D<float> currentDepthTexture = GetTexture2DFloat(g_Pass.CurrentDepthIndex);
	RWTexture2D<float4> resolvedColor =
		GetRWTexture2DFloat4(g_Pass.ResolvedColorUavIndex);
	RWTexture2D<float4> nextHistoryColor =
		GetRWTexture2DFloat4(g_Pass.NextHistoryColorUavIndex);
	RWTexture2D<float> nextHistoryDepth =
		GetRWTexture2DFloat(g_Pass.NextHistoryDepthUavIndex);
	RWTexture2D<float4> reprojectionDiagnostics =
		GetRWTexture2DFloat4(g_Pass.ReprojectionDiagnosticsUavIndex);

	const float2 currentUV = (float2(pixel) + 0.5.xx) / float2(width, height);
	const float currentRawDepth = currentDepthTexture.Load(int3(pixel, 0));
	const ViewData viewData = g_Views[g_Scene.ViewBaseIndex + g_Pass.ViewIndex];
	float3 currentColor = currentColorTexture.Load(int3(pixel, 0)).rgb;
	if (!IsTemporalColorFinite(currentColor))
	{
		currentColor = 0.0.xxx;
	}

	uint rejectionReason = TAA_REJECTION_HISTORY_UNAVAILABLE;
	float2 previousUV = currentUV;
	bool accepted = false;
	float3 historyColor = currentColor;
	if (g_Pass.PreviousHistoryValid != 0)
	{
		if (IsDepthBackground(currentRawDepth, viewData.DepthConvention))
		{
			previousUV = ReprojectTemporalSkyUV(currentUV, viewData);
		}
		else
		{
			const float2 motionUV = motionTexture.Load(int3(pixel, 0));
			previousUV = ReprojectTemporalUV(currentUV, motionUV);
		}

		if (!IsTemporalUVInBounds(previousUV))
		{
			rejectionReason = all(isfinite(previousUV))
				? TAA_REJECTION_PREVIOUS_UV_OUT_OF_BOUNDS
				: TAA_REJECTION_NON_FINITE;
		}
		else
		{
			Texture2D<float4> previousColorTexture =
				GetTexture2DFloat4(g_Pass.PreviousColorIndex);
			SamplerState linearClampSampler =
				GetSamplerState(g_Pass.LinearClampSamplerIndex);
			historyColor = previousColorTexture.SampleLevel(
				linearClampSampler, previousUV, 0.0).rgb;
			if (!IsTemporalColorFinite(historyColor))
			{
				rejectionReason = TAA_REJECTION_NON_FINITE;
			}
			else if (IsDepthBackground(currentRawDepth, viewData.DepthConvention))
			{
				accepted = true;
				rejectionReason = TAA_REJECTION_NONE;
			}
			else
			{
				Texture2D<float> previousDepthTexture =
					GetTexture2DFloat(g_Pass.PreviousDepthIndex);
				SamplerState pointClampSampler =
					GetSamplerState(g_Pass.PointClampSamplerIndex);
				accepted = ValidateTemporalGeometryDepth(currentUV, currentRawDepth,
					previousUV, previousDepthTexture, pointClampSampler, viewData,
					g_Pass.DepthAbsoluteThreshold, g_Pass.DepthRelativeThreshold);
				rejectionReason = accepted ? TAA_REJECTION_NONE : TAA_REJECTION_DEPTH_MISMATCH;
			}
		}
	}

	const float historyWeight = accepted
		? saturate(g_Pass.RestrictedHistoryWeight)
		: 0.0;
	float3 outputColor = lerp(currentColor, historyColor, historyWeight);
	if (!IsTemporalColorFinite(outputColor))
	{
		outputColor = currentColor;
		accepted = false;
		rejectionReason = TAA_REJECTION_NON_FINITE;
	}

	const float4 output = float4(outputColor, 1.0);
	resolvedColor[pixel] = output;
	nextHistoryColor[pixel] = output;
	nextHistoryDepth[pixel] = currentRawDepth;
	reprojectionDiagnostics[pixel] = float4(
		accepted ? 1.0 : 0.0, float(rejectionReason), previousUV);
}
