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

FullscreenTriangleVSOutput VSMain(uint vertexId : SV_VertexID)
{
	return FullscreenTriangleVS(vertexId);
}

float4 PSMain(FullscreenTriangleVSOutput input) : SV_Target
{
	const uint viewIndex = g_Scene.ViewBaseIndex + g_Pass.ViewIndex;
	const ViewData viewData = g_Views[viewIndex];
	if (g_Pass.SourceMode == PREVIEW_SOURCE_SCENE_DEPTH_RAW ||
		g_Pass.SourceMode == PREVIEW_SOURCE_SCENE_DEPTH_LINEAR_VIEW_Z)
	{
		Texture2D<float> depthTexture = GetTexture2DFloat(g_Pass.SourceTextureIndex);
		SamplerState pointSampler = GetSamplerState(g_Pass.SourceSamplerIndex);
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
