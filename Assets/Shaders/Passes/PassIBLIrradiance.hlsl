#include <Common/Common.hlsli>
#include <Common/Sampling.hlsli>
#include <Common/FullscreenTriangle.hlsli>
#include <Common/Cubemap.hlsli>
#include <Common/MaterialSampling.hlsli>
#include <Common/ApplicationBinding.hlsli>

cbuffer IBLIrradiancePassConstants : register(b2)
{
	uint g_CubemapFaceIndex;
	uint g_EnvironmentTextureIndex;
	uint g_EnvironmentSamplerIndex;
	uint g_IBLIrradiancePassPadding;
};

TextureSamplerBindingData GetEnvironmentBinding()
{
	return MakeTextureSamplerBinding(uint2(g_EnvironmentTextureIndex, g_EnvironmentSamplerIndex));
}

float3 IntegrateIrradiance(TextureSamplerBindingData environmentBinding, float3 normalWS)
{
	const uint SAMPLE_COUNT = 1024;
	float3 irradiance = 0.0.xxx;

	for (uint i = 0; i < SAMPLE_COUNT; ++i)
	{
		float2 Xi = Hammersley(i, SAMPLE_COUNT);
		// Directions are sampled using a cosine-weighted hemisphere PDF:
		// p(L) = NoL / PI.
		// Therefore the NoL term in the irradiance integral is cancelled
		// by the PDF denominator, leaving only PI * average(radiance).
		float3 directionTS = CosineSampleHemisphere(Xi);
		float3 directionWS = TangentToWorld(directionTS, normalWS);

		irradiance += SampleTextureCubeLevel(environmentBinding, directionWS, 0.0).rgb;
	}

	return PI * irradiance / SAMPLE_COUNT;
}

FullscreenTriangleVSOutput VSMain(uint vid : SV_VertexID)
{
	return FullscreenTriangleVS(vid);
}

float4 PSMain(FullscreenTriangleVSOutput IN) : SV_Target0
{
	float3 normalWS = CubemapFaceUvToDirection(g_CubemapFaceIndex, IN.UV);
	TextureSamplerBindingData environmentBinding = GetEnvironmentBinding();

	return float4(IntegrateIrradiance(environmentBinding, normalWS), 1.0);
}
