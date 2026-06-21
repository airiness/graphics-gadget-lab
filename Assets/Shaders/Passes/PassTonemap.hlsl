#include <Common/Common.hlsli>
#include <Common/FullscreenTriangle.hlsli>
#include <Common/MaterialSampling.hlsli>
#include <Common/ApplicationBinding.hlsli>

struct TonemapPassParameters
{
	uint SceneColorTextureIndex;
	uint SceneColorSamplerIndex;
	uint ViewIndex;
	uint Padding;
};

ConstantBuffer<TonemapPassParameters> g_Pass : register(b2);

FullscreenTriangleVSOutput VSMain(uint vertexId : SV_VertexID)
{
	return FullscreenTriangleVS(vertexId);
}

float4 PSMain(FullscreenTriangleVSOutput IN) : SV_Target
{
	const uint viewIndex = g_Scene.ViewBaseIndex + g_Pass.ViewIndex;
	const ViewData viewData = g_Views[viewIndex];

	const float3 hdrColor = SampleTexture2D(
		g_Pass.SceneColorTextureIndex,
		g_Pass.SceneColorSamplerIndex,
		IN.UV).rgb;

	float3 color = ACESFitted(hdrColor * viewData.Exposure);
	color = LinearToSRGB(color);

	return float4(color, 1.0);
}
