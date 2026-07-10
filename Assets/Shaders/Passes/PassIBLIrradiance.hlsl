#include <Common/Common.hlsli>
#include <Common/Sampling.hlsli>
#include <Common/FullscreenTriangle.hlsli>
#include <Common/Cubemap.hlsli>
#include <Common/MaterialSampling.hlsli>
#include <Common/ApplicationBinding.hlsli>

struct IBLIrradiancePassParameters
{
	uint CubemapFaceIndex;
	uint EnvironmentTextureIndex;
	uint EnvironmentSamplerIndex;
	uint EnvironmentResolution;
	uint EnvironmentMipLevels;
	uint3 Padding;
};

ConstantBuffer<IBLIrradiancePassParameters> g_Pass : register(b2);

TextureSamplerBindingData GetEnvironmentBinding()
{
	return MakeTextureSamplerBinding(uint2(g_Pass.EnvironmentTextureIndex, g_Pass.EnvironmentSamplerIndex));
}

float3 IntegrateIrradiance(TextureSamplerBindingData environmentBinding, float3 normalWS)
{
	const uint SAMPLE_COUNT = 1024;
	const float environmentResolution = max((float) g_Pass.EnvironmentResolution, 1.0);
	const float environmentTexelSolidAngle =
		4.0 * PI / (6.0 * environmentResolution * environmentResolution);
	const float maxEnvironmentMip = (float) (max(g_Pass.EnvironmentMipLevels, 1u) - 1u);
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
		float NoL = saturate(directionTS.z);
		float pdf = NoL * INV_PI;
		float sampleSolidAngle = 1.0 / max((float) SAMPLE_COUNT * pdf, 1.0e-6);
		float sourceMip = 0.5 * log2(sampleSolidAngle / environmentTexelSolidAngle);
		sourceMip = clamp(sourceMip, 0.0, maxEnvironmentMip);

		irradiance += SanitizeHDRColor(
			SampleTextureCubeLevel(environmentBinding, directionWS, sourceMip).rgb);
	}

	return SanitizeHDRColor(PI * irradiance / SAMPLE_COUNT);
}

FullscreenTriangleVSOutput VSMain(uint vid : SV_VertexID)
{
	return FullscreenTriangleVS(vid);
}

float4 PSMain(FullscreenTriangleVSOutput IN) : SV_Target0
{
	float3 normalWS = CubemapFaceUvToDirection(g_Pass.CubemapFaceIndex, IN.UV);
	TextureSamplerBindingData environmentBinding = GetEnvironmentBinding();

	return float4(IntegrateIrradiance(environmentBinding, normalWS), 1.0);
}
