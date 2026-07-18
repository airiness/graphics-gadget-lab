#include <Common/Common.hlsli>
#include <Common/FullscreenTriangle.hlsli>
#include <Common/MaterialSampling.hlsli>
#include <Common/ApplicationBinding.hlsli>

struct PostProcessPreviewPassParameters
{
	uint SourceTextureIndex;
	uint SourceSamplerIndex;
	uint ViewIndex;
	uint Padding0;
	float SourcePreExposure;
	float PreviewExposureScale;
	float Padding1;
	float Padding2;
};

ConstantBuffer<PostProcessPreviewPassParameters> g_Pass : register(b2);

FullscreenTriangleVSOutput VSMain(uint vertexId : SV_VertexID)
{
	return FullscreenTriangleVS(vertexId);
}

float4 PSMain(FullscreenTriangleVSOutput input) : SV_Target
{
	const uint viewIndex = g_Scene.ViewBaseIndex + g_Pass.ViewIndex;
	const ViewData viewData = g_Views[viewIndex];
	const float exposureScaleOverPreExposure =
		viewData.ExposureMultiplier / max(g_Pass.SourcePreExposure, 1e-6);
	const float3 storedColor = SanitizeHDRColor(SampleTexture2D(
		g_Pass.SourceTextureIndex,
		g_Pass.SourceSamplerIndex,
		input.UV).rgb);
	float3 color = ACESFitted(
		storedColor * exposureScaleOverPreExposure * g_Pass.PreviewExposureScale);
	color = LinearToSRGB(color);
	return float4(color, 1.0);
}
