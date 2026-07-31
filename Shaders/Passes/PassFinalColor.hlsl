#include <Common/Common.hlsli>
#include <Common/FullscreenTriangle.hlsli>
#include <Common/MaterialSampling.hlsli>
#include <Common/ApplicationBinding.hlsli>

struct FinalColorPassParameters
{
	uint SceneColorTextureIndex;
	uint SceneColorSamplerIndex;
	uint BloomTextureIndex;
	uint BloomSamplerIndex;
	uint ViewIndex;
	uint BloomEnabled;
	float BloomIntensity;
	float ScenePreExposure;
};

ConstantBuffer<FinalColorPassParameters> g_Pass : register(b2);

FullscreenTriangleVSOutput VSMain(uint vertexId : SV_VertexID)
{
	return FullscreenTriangleVS(vertexId);
}

float4 PSMain(FullscreenTriangleVSOutput IN) : SV_Target
{
	const uint viewIndex = g_Scene.ViewBaseIndex + g_Pass.ViewIndex;
	const ViewData viewData = g_Views[viewIndex];

	float3 storedColor = SanitizeHDRColor(
		SampleTexture2D(g_Pass.SceneColorTextureIndex, g_Pass.SceneColorSamplerIndex, IN.UV).rgb);
	if (g_Pass.BloomEnabled != 0)
	{
		storedColor +=
			SanitizeHDRColor(
				SampleTexture2D(g_Pass.BloomTextureIndex, g_Pass.BloomSamplerIndex, IN.UV).rgb) *
			g_Pass.BloomIntensity;
	}

	const float exposureScaleOverPreExposure =
		viewData.ExposureMultiplier / max(g_Pass.ScenePreExposure, 1e-6);
	float3 color = ACESFitted(storedColor * exposureScaleOverPreExposure);
	color = LinearToSRGB(color);

	return float4(color, 1.0);
}
