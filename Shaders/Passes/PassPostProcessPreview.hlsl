#include <Common/NonFiniteCounters.hlsli>
#include <Common/Common.hlsli>
#include <Common/BindlessResources.hlsli>
#include <Common/DepthReconstruction.hlsli>
#include <Common/FullscreenTriangle.hlsli>
#include <Common/MaterialSampling.hlsli>
#include <Common/ApplicationBinding.hlsli>
#include <Lighting/GIShaderConstants.hlsli>

struct PostProcessPreviewPassParameters
{
	uint SourceTextureIndex;
	uint SourceSamplerIndex;
	uint ViewIndex;
	uint SourceMode;
	float SourcePreExposure;
	float PreviewExposureScale;
	uint ValidityTextureIndex;
	uint ActivityTextureIndex;
};

ConstantBuffer<PostProcessPreviewPassParameters> g_Pass : register(b2);

static const uint PREVIEW_SOURCE_SCENE_DEPTH_RAW = 4;
static const uint PREVIEW_SOURCE_SCENE_DEPTH_LINEAR_VIEW_Z = 5;
static const uint PREVIEW_SOURCE_GTAO_RAW_AO = 6;
static const uint PREVIEW_SOURCE_GTAO_HALF_DEPTH_VIEW_Z = 7;
static const uint PREVIEW_SOURCE_GTAO_RECONSTRUCTED_NORMAL = 8;
static const uint PREVIEW_SOURCE_GTAO_SELECTED_SURFACE_OFFSET = 9;
static const uint PREVIEW_SOURCE_GTAO_DENOISE_X = 10;
static const uint PREVIEW_SOURCE_GTAO_DENOISE_Y = 11;
static const uint PREVIEW_SOURCE_GTAO_FINAL_AO = 12;
static const uint PREVIEW_SOURCE_GTAO_AO_ONLY_LIGHTING_CONTRIBUTION = 13;
static const uint PREVIEW_SOURCE_TEMPORAL_MOTION_DIRECTION = 14;
static const uint PREVIEW_SOURCE_TEMPORAL_MOTION_MAGNITUDE = 15;
static const uint PREVIEW_SOURCE_TEMPORAL_HISTORY_COLOR = 16;
static const uint PREVIEW_SOURCE_TEMPORAL_REPROJECTION_UV = 17;
static const uint PREVIEW_SOURCE_TEMPORAL_REJECTION = 18;
static const uint PREVIEW_SOURCE_TEMPORAL_HISTORY_WEIGHT = 19;
static const uint PREVIEW_SOURCE_TEMPORAL_HISTORY_SAMPLES = 20;
static const uint PREVIEW_SOURCE_TEMPORAL_CLIP_DISTANCE = 26;
static const uint PREVIEW_SOURCE_TEMPORAL_HISTORY_RELAXATION = 27;
static const uint PREVIEW_SOURCE_GTAO_HISTORY_SAMPLES = 28;

FullscreenTriangleVSOutput VSMain(uint vertexId : SV_VertexID)
{
	return FullscreenTriangleVS(vertexId);
}

float4 PSMain(FullscreenTriangleVSOutput input) : SV_Target
{
	const uint viewIndex = g_Scene.ViewBaseIndex + g_Pass.ViewIndex;
	const ViewData viewData = g_Views[viewIndex];
	SamplerState pointSampler = GetSamplerState(g_Pass.SourceSamplerIndex);
	const bool giVisibility = g_Pass.SourceMode == GIPreviewBaseVisibility ||
		g_Pass.SourceMode == GIPreviewCoatVisibility || g_Pass.SourceMode == GIPreviewCombinedVisibility;
	const bool giDiffuse = g_Pass.SourceMode == GIPreviewIrradiance ||
		g_Pass.SourceMode == GIPreviewLambertRadiance || g_Pass.SourceMode == GIPreviewSunIrradiance ||
		g_Pass.SourceMode == GIPreviewEnvironmentIrradiance;
	if (giDiffuse || giVisibility)
	{
		const float4 value = GetTexture2DFloat4(g_Pass.SourceTextureIndex)
			.SampleLevel(pointSampler, input.UV, 0);
		const float validity = GetTexture2DFloat4(g_Pass.ValidityTextureIndex)
			.SampleLevel(pointSampler, input.UV, 0).a;
		if (!all(isfinite(NonFiniteObserveValue(NF_SITE_PASS_POST_PROCESS_PREVIEW_PSMAIN_ISFINITE, value))) ||
			!isfinite(NonFiniteObserveValue(NF_SITE_PASS_POST_PROCESS_PREVIEW_PSMAIN_ISFINITE, validity)) || validity < 0.0)
		{
			return float4(1, 0, 1, 1);
		}
		if (validity == 0.0)
		{
			const uint2 cell = (uint2)(input.PositionCS.xy) / 8u;
			return float4((((cell.x + cell.y) & 1u) != 0u ? 0.12 : 0.18).xxx, 1);
		}
		if (giVisibility)
		{
			const uint active = (uint)(GetTexture2DFloat4(g_Pass.ActivityTextureIndex)
				.SampleLevel(pointSampler, input.UV, 0).g);
			const uint channel = g_Pass.SourceMode == GIPreviewBaseVisibility ? 0u :
				g_Pass.SourceMode == GIPreviewCoatVisibility ? 1u : 2u;
			const uint lobe = g_Pass.SourceMode == GIPreviewBaseVisibility ? GIVisibilityBaseActive :
				g_Pass.SourceMode == GIPreviewCoatVisibility ? GIVisibilityCoatActive :
				GIVisibilityBaseActive | GIVisibilityCoatActive;
			const bool inactive = validity == 2.0 || (active & lobe) == 0u;
			return inactive ? float4(0.12, 0.25, 0.4, 1) : float4(saturate(value[channel]).xxx, 1);
		}
		// GI values are scene referred, independent of the camera's exposure/pre-exposure.
		const float3 color = ACESFitted(max(value.rgb, 0.0) * g_Pass.PreviewExposureScale,
			NF_SITE_PASS_POST_PROCESS_PREVIEW_PSMAIN_ACESFITTED);
		return float4(LinearToSRGB(color), 1);
	}
	if (g_Pass.SourceMode >= 21 && g_Pass.SourceMode <= 23)
	{
		float4 sampleValue = GetTexture2DFloat4(g_Pass.SourceTextureIndex).SampleLevel(pointSampler, input.UV, 0);
		if (sampleValue.a < 0.999 || !all(isfinite(NonFiniteObserveValue(NF_SITE_PASS_POST_PROCESS_PREVIEW_PSMAIN_ISFINITE, sampleValue))))
		{
			return float4(1, 0, 1, 1);
		}
		// Dimensionless transmittance and unit-illuminance scattering do not use camera exposure.
		float3 color = sampleValue.rgb;
		if (g_Pass.SourceMode == 21)
		{
			return float4(saturate(color), 1);
		}
		float exposure = g_Pass.SourceMode == 23 ? viewData.ExposureMultiplier : 1.0;
		return float4(LinearToSRGB(ACESFitted(color * exposure * g_Pass.PreviewExposureScale, NF_SITE_PASS_POST_PROCESS_PREVIEW_PSMAIN_ACESFITTED)), 1);
	}
	if (g_Pass.SourceMode == 24)
	{
		return float4(saturate(GetTexture2DFloat4(g_Pass.SourceTextureIndex)
			.SampleLevel(pointSampler, input.UV, 0).rgb), 1.0);
	}
	if (g_Pass.SourceMode == PREVIEW_SOURCE_TEMPORAL_MOTION_DIRECTION ||
		g_Pass.SourceMode == PREVIEW_SOURCE_TEMPORAL_MOTION_MAGNITUDE)
	{
		Texture2D<float2> motionTexture = GetTexture2DFloat2(g_Pass.SourceTextureIndex);
		const float2 motionUV = motionTexture.SampleLevel(pointSampler, input.UV, 0.0);
		const float2 motionPixels = motionUV * float2(viewData.Width, viewData.Height);
		const float magnitudePixels = length(motionPixels);
		if (g_Pass.SourceMode == PREVIEW_SOURCE_TEMPORAL_MOTION_MAGNITUDE)
		{
			return float4(saturate(magnitudePixels / 8.0).xxx, 1.0);
		}
		const float2 direction = magnitudePixels > 1.0e-6 ?
			motionPixels / magnitudePixels :
			float2(0.0, 0.0);
		return float4(direction * 0.5 + 0.5, saturate(magnitudePixels / 8.0), 1.0);
	}
	if (g_Pass.SourceMode == PREVIEW_SOURCE_TEMPORAL_REPROJECTION_UV ||
		g_Pass.SourceMode == PREVIEW_SOURCE_TEMPORAL_REJECTION ||
		g_Pass.SourceMode == PREVIEW_SOURCE_TEMPORAL_HISTORY_WEIGHT ||
		g_Pass.SourceMode == PREVIEW_SOURCE_TEMPORAL_HISTORY_SAMPLES ||
		g_Pass.SourceMode == PREVIEW_SOURCE_TEMPORAL_CLIP_DISTANCE ||
		g_Pass.SourceMode == PREVIEW_SOURCE_TEMPORAL_HISTORY_RELAXATION)
	{
		Texture2D<float4> diagnosticsTexture = GetTexture2DFloat4(g_Pass.SourceTextureIndex);
		const float4 diagnostics = diagnosticsTexture.SampleLevel(pointSampler, input.UV, 0.0);
		if (g_Pass.SourceMode == PREVIEW_SOURCE_TEMPORAL_REPROJECTION_UV)
		{
			return float4(saturate(diagnostics.zw), 0.0, 1.0);
		}
		if (g_Pass.SourceMode == PREVIEW_SOURCE_TEMPORAL_HISTORY_WEIGHT ||
			g_Pass.SourceMode == PREVIEW_SOURCE_TEMPORAL_HISTORY_SAMPLES ||
			g_Pass.SourceMode == PREVIEW_SOURCE_TEMPORAL_CLIP_DISTANCE ||
			g_Pass.SourceMode == PREVIEW_SOURCE_TEMPORAL_HISTORY_RELAXATION)
		{
			return float4(saturate(diagnostics.x).xxx, 1.0);
		}
		const float reason = diagnostics.y;
		if (reason < 0.5)
		{
			return float4(0.1, 0.8, 0.2, 1.0);
		}
		return float4(saturate(reason / 5.0), 0.05, 1.0 - saturate(reason / 5.0), 1.0);
	}
	if (g_Pass.SourceMode == PREVIEW_SOURCE_GTAO_RAW_AO ||
		g_Pass.SourceMode == PREVIEW_SOURCE_GTAO_DENOISE_X ||
		g_Pass.SourceMode == PREVIEW_SOURCE_GTAO_DENOISE_Y ||
		g_Pass.SourceMode == PREVIEW_SOURCE_GTAO_FINAL_AO ||
		g_Pass.SourceMode == PREVIEW_SOURCE_GTAO_HISTORY_SAMPLES)
	{
		Texture2D<float> aoTexture = GetTexture2DFloat(g_Pass.SourceTextureIndex);
		const float ao = aoTexture.SampleLevel(pointSampler, input.UV, 0.0);
		return float4(ao.xxx, 1.0);
	}
	if (g_Pass.SourceMode == PREVIEW_SOURCE_GTAO_HALF_DEPTH_VIEW_Z)
	{
		Texture2D<float> depthTexture = GetTexture2DFloat(g_Pass.SourceTextureIndex);
		const float viewZ = depthTexture.SampleLevel(pointSampler, input.UV, 0.0);
		if (!isfinite(NonFiniteObserveValue(NF_SITE_PASS_POST_PROCESS_PREVIEW_PSMAIN_ISFINITE, viewZ)) || viewZ <= 0.0)
		{
			return float4(0.0, 0.0, 0.0, 1.0);
		}
		const float depthRange = max(viewData.Far / max(viewData.Near, 1.0e-6), 1.0);
		const float normalizedViewZ = saturate(
			log2(max(viewZ / max(viewData.Near, 1.0e-6), 1.0)) / max(log2(depthRange), 1.0e-6));
		return float4((1.0 - normalizedViewZ).xxx, 1.0);
	}
	if (g_Pass.SourceMode == PREVIEW_SOURCE_GTAO_RECONSTRUCTED_NORMAL)
	{
		Texture2D<float4> normalTexture = GetTexture2DFloat4(g_Pass.SourceTextureIndex);
		const float4 normalAndValidity = normalTexture.SampleLevel(pointSampler, input.UV, 0.0);
		return normalAndValidity.w > 0.0 ?
			float4(normalAndValidity.xyz * 0.5 + 0.5, 1.0) :
			float4(0.0, 0.0, 0.0, 1.0);
	}
	if (g_Pass.SourceMode == PREVIEW_SOURCE_GTAO_SELECTED_SURFACE_OFFSET)
	{
		Texture2D<float2> offsetTexture = GetTexture2DFloat2(g_Pass.SourceTextureIndex);
		const float2 offset = offsetTexture.SampleLevel(pointSampler, input.UV, 0.0);
		return any(offset < 0.0) ?
			float4(0.0, 0.0, 0.0, 1.0) :
			float4(offset, 1.0 - 0.5 * (offset.x + offset.y), 1.0);
	}
	if (g_Pass.SourceMode == PREVIEW_SOURCE_SCENE_DEPTH_RAW ||
		g_Pass.SourceMode == PREVIEW_SOURCE_SCENE_DEPTH_LINEAR_VIEW_Z)
	{
		Texture2D<float> depthTexture = GetTexture2DFloat(g_Pass.SourceTextureIndex);
		const float rawDepth = depthTexture.SampleLevel(pointSampler, input.UV, 0.0);
		if (g_Pass.SourceMode == PREVIEW_SOURCE_SCENE_DEPTH_RAW)
		{
			return float4(rawDepth.xxx, 1.0);
		}
		if (IsDepthBackground(rawDepth, viewData.DepthConvention))
		{
			return float4(0.0, 0.0, 0.0, 1.0);
		}

		const float viewZ = RawDepthToPositiveViewZ(input.UV, rawDepth, viewData.InvProjMat);
		const float depthRange = max(viewData.Far / max(viewData.Near, 1.0e-6), 1.0);
		const float normalizedViewZ = saturate(
			log2(max(viewZ / max(viewData.Near, 1.0e-6), 1.0)) / max(log2(depthRange), 1.0e-6));
		return float4((1.0 - normalizedViewZ).xxx, 1.0);
	}

	const float exposureScaleOverPreExposure =
		ExposureScaleOverPreExposure(viewData.ExposureMultiplier, g_Pass.SourcePreExposure);
	const float3 storedColor = SanitizeHDRColor(
		SampleTexture2D(g_Pass.SourceTextureIndex, g_Pass.SourceSamplerIndex, input.UV).rgb, NF_SITE_PASS_POST_PROCESS_PREVIEW_PSMAIN_SANITIZE_HDRCOLOR);
	float3 color =
		ACESFitted(storedColor * exposureScaleOverPreExposure * g_Pass.PreviewExposureScale, NF_SITE_PASS_POST_PROCESS_PREVIEW_PSMAIN_ACESFITTED);
	color = LinearToSRGB(color);
	return float4(color, 1.0);
}
