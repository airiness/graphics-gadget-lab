#include <Common/Common.hlsli>
#include <Common/FullscreenTriangle.hlsli>
#include <Common/MaterialSampling.hlsli>
#include <Common/ApplicationBinding.hlsli>

cbuffer TonemapPassConstants : register(b2)
{
	uint g_SceneColorTextureIndex;
	uint g_SceneColorSamplerIndex;
	uint g_ViewIndex;
	uint g_TonemapPassPadding;
};

FullscreenTriangleVSOutput VSMain(uint vertexId : SV_VertexID)
{
	return FullscreenTriangleVS(vertexId);
}

float4 PSMain(FullscreenTriangleVSOutput IN) : SV_Target
{
	const uint viewIndex = g_Scene.ViewBaseIndex + g_ViewIndex;
	const ViewData viewData = g_Views[viewIndex];

	const float3 hdrColor = SampleTexture2D(
		g_SceneColorTextureIndex,
		g_SceneColorSamplerIndex,
		IN.UV).rgb;

	float3 color = ACESFitted(hdrColor * viewData.Exposure);
	color = LinearToSRGB(color);

	return float4(color, 1.0);
}
