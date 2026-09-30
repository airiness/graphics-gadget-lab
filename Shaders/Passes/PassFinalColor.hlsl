#include <Common/Common.hlsli>
#include <Common/DisplayColor.hlsli>
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
	uint MaterialDiagnosticColorIndex;
	uint MaterialDiagnosticCoverageIndex;
	uint MaterialDiagnosticsEnabled;
	uint Padding;
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
		ExposureScaleOverPreExposure(viewData.ExposureMultiplier, g_Pass.ScenePreExposure);
	float3 diagnosticColor = 0.0.xxx;
	float diagnosticCoverage = 0.0;
	if (g_Pass.MaterialDiagnosticsEnabled != 0u)
	{
		const int3 pixel = int3(uint2(IN.PositionCS.xy), 0);
		diagnosticColor = GetTexture2DFloat4(g_Pass.MaterialDiagnosticColorIndex).Load(pixel).rgb;
		diagnosticCoverage = GetTexture2DFloat(g_Pass.MaterialDiagnosticCoverageIndex).Load(pixel);
	}
	const float3 color = ResolveDisplayColor(storedColor, exposureScaleOverPreExposure,
		diagnosticColor, diagnosticCoverage);

	return float4(color, 1.0);
}
