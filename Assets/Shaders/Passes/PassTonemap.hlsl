#include <Common/Common.hlsli>
#include <Common/FullscreenTriangle.hlsli>
#include <Common/MaterialSampling.hlsli>
#include <Common/ApplicationBinding.hlsli>

uint GetSceneColorTextureIndex()
{
	return g_LocalParam0;
}

uint GetSceneColorSamplerIndex()
{
	return g_LocalParam1;
}

uint GetViewIndex()
{
	return g_LocalParam2;
}

FullscreenTriangleVSOutput VSMain(uint vertexId : SV_VertexID)
{
	return FullscreenTriangleVS(vertexId);
}

float4 PSMain(FullscreenTriangleVSOutput IN) : SV_Target
{
	const uint viewIndex = g_Scene.ViewBaseIndex + GetViewIndex();
	const ViewData viewData = g_Views[viewIndex];

	const float3 hdrColor = SampleTexture2D(
		GetSceneColorTextureIndex(),
		GetSceneColorSamplerIndex(),
		IN.UV).rgb;

	float3 color = ACESFitted(hdrColor * viewData.Exposure);
	color = LinearToSRGB(color);

	return float4(color, 1.0);
}
