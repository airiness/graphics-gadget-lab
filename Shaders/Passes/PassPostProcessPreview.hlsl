#include <Common/Common.hlsli>
#include <Common/BindlessResources.hlsli>
#include <Common/DepthReconstruction.hlsli>
#include <Common/FullscreenTriangle.hlsli>
#include <Common/MaterialSampling.hlsli>
#include <Common/ApplicationBinding.hlsli>

struct PostProcessPreviewPassParameters
{
	uint SourceTextureIndex;
	uint SourceSamplerIndex;
	uint ViewIndex;
	uint SourceMode;
	float SourcePreExposure;
	float PreviewExposureScale;
	float Padding1;
	float Padding2;
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

FullscreenTriangleVSOutput VSMain(uint vertexId : SV_VertexID)
{
	return FullscreenTriangleVS(vertexId);
}

float4 PSMain(FullscreenTriangleVSOutput input) : SV_Target
{
	const uint viewIndex = g_Scene.ViewBaseIndex + g_Pass.ViewIndex;
	const ViewData viewData = g_Views[viewIndex];
	SamplerState pointSampler = GetSamplerState(g_Pass.SourceSamplerIndex);
	if (g_Pass.SourceMode == PREVIEW_SOURCE_GTAO_RAW_AO ||
		g_Pass.SourceMode == PREVIEW_SOURCE_GTAO_DENOISE_X ||
		g_Pass.SourceMode == PREVIEW_SOURCE_GTAO_DENOISE_Y ||
		g_Pass.SourceMode == PREVIEW_SOURCE_GTAO_FINAL_AO)
	{
		Texture2D<float> aoTexture = GetTexture2DFloat(g_Pass.SourceTextureIndex);
		const float ao = aoTexture.SampleLevel(pointSampler, input.UV, 0.0);
		return float4(ao.xxx, 1.0);
	}
	if (g_Pass.SourceMode == PREVIEW_SOURCE_GTAO_HALF_DEPTH_VIEW_Z)
	{
		Texture2D<float> depthTexture = GetTexture2DFloat(g_Pass.SourceTextureIndex);
		const float viewZ = depthTexture.SampleLevel(pointSampler, input.UV, 0.0);
		if (!isfinite(viewZ) || viewZ <= 0.0)
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
		return normalAndValidity.w > 0.0
			? float4(normalAndValidity.xyz * 0.5 + 0.5, 1.0)
			: float4(0.0, 0.0, 0.0, 1.0);
	}
	if (g_Pass.SourceMode == PREVIEW_SOURCE_GTAO_SELECTED_SURFACE_OFFSET)
	{
		Texture2D<float2> offsetTexture = GetTexture2DFloat2(g_Pass.SourceTextureIndex);
		const float2 offset = offsetTexture.SampleLevel(pointSampler, input.UV, 0.0);
		return any(offset < 0.0)
			? float4(0.0, 0.0, 0.0, 1.0)
			: float4(offset, 1.0 - 0.5 * (offset.x + offset.y), 1.0);
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
		viewData.ExposureMultiplier / max(g_Pass.SourcePreExposure, 1e-6);
	const float3 storedColor = SanitizeHDRColor(
		SampleTexture2D(g_Pass.SourceTextureIndex, g_Pass.SourceSamplerIndex, input.UV).rgb);
	float3 color =
		ACESFitted(storedColor * exposureScaleOverPreExposure * g_Pass.PreviewExposureScale);
	color = LinearToSRGB(color);
	return float4(color, 1.0);
}
