#include <Common/Common.hlsli>
#include <Common/Sampling.hlsli>
#include <Common/FullscreenTriangle.hlsli>
#include <Common/Cubemap.hlsli>
#include <Common/MaterialSampling.hlsli>
#include <Common/ApplicationBinding.hlsli>
#include <PBR/BRDF.hlsli>

struct IBLPrefilteredSpecularPassParameters
{
	uint CubemapFaceIndex;
	uint MipLevel;
	uint MipLevels;
	uint EnvironmentTextureIndex;
	uint EnvironmentSamplerIndex;
	uint EnvironmentResolution;
	uint EnvironmentMipLevels;
	uint SampleCount;
	float MaxSampleLuminance;
	uint3 Padding;
};

ConstantBuffer<IBLPrefilteredSpecularPassParameters> g_Pass : register(b2);

TextureSamplerBindingData GetEnvironmentBinding()
{
	return MakeTextureSamplerBinding(
		uint2(g_Pass.EnvironmentTextureIndex, g_Pass.EnvironmentSamplerIndex));
}

float GetPerceptualRoughness()
{
	uint mipLevels = max(g_Pass.MipLevels, 1u);
	return mipLevels > 1u ? (float) g_Pass.MipLevel / (float) (mipLevels - 1u) : 0.0;
}

float3 ClampSampleLuminance(float3 radiance)
{
	radiance = SanitizeHDRColor(radiance);
	const float luminance = dot(radiance, float3(0.2126, 0.7152, 0.0722));
	const float maxLuminance = max(g_Pass.MaxSampleLuminance, 1.0);
	return luminance > maxLuminance ? radiance * (maxLuminance / luminance) : radiance;
}

float3 IntegratePrefilteredSpecular(
	TextureSamplerBindingData environmentBinding, float3 normalWS, float perceptualRoughness)
{
	// GGX degenerates to a delta distribution at zero roughness. Preserve the
	// original environment texel instead of integrating an ill-conditioned PDF.
	if (g_Pass.MipLevel == 0u || perceptualRoughness <= 0.0)
	{
		return SanitizeHDRColor(SampleTextureCubeLevel(environmentBinding, normalWS, 0.0).rgb);
	}

	const uint sampleCount = max(g_Pass.SampleCount, 1u);
	const float environmentResolution = max((float) g_Pass.EnvironmentResolution, 1.0);
	const float environmentTexelSolidAngle =
		4.0 * PI / (6.0 * environmentResolution * environmentResolution);
	const float maxEnvironmentMip = (float) (max(g_Pass.EnvironmentMipLevels, 1u) - 1u);
	float alpha = PerceptualRoughnessToAlpha(perceptualRoughness);
	float3 viewWS = normalWS;
	float3 prefilteredColor = 0.0.xxx;
	float totalWeight = 0.0;

	for (uint i = 0; i < sampleCount; ++i)
	{
		float2 Xi = Hammersley(i, sampleCount);
		float3 halfTS = ImportanceSampleGGX(Xi, alpha);
		float3 halfWS = TangentToWorld(halfTS, normalWS);
		float3 lightWS = normalize(2.0 * dot(viewWS, halfWS) * halfWS - viewWS);

		float NoL = saturate(dot(normalWS, lightWS));
		if (NoL > 0.0)
		{
			float NoH = saturate(dot(normalWS, halfWS));
			float HoV = saturate(dot(halfWS, viewWS));
			float pdf = D_GGX(NoH, alpha) * NoH / max(4.0 * HoV, 1.0e-6);

			// Match the solid angle represented by one importance sample to the
			// cubemap texel footprint, reducing high-frequency noise without biasing
			// every roughness level toward environment mip 0.
			float sampleSolidAngle = 1.0 / max((float) sampleCount * pdf, 1.0e-6);
			float sourceMip = 0.5 * log2(sampleSolidAngle / environmentTexelSolidAngle);
			sourceMip = clamp(sourceMip, 0.0, maxEnvironmentMip);

			float3 sampleRadiance =
				SampleTextureCubeLevel(environmentBinding, lightWS, sourceMip).rgb;
			prefilteredColor += ClampSampleLuminance(sampleRadiance) * NoL;
			totalWeight += NoL;
		}
	}

	return SanitizeHDRColor(prefilteredColor / max(totalWeight, 1e-5));
}

FullscreenTriangleVSOutput VSMain(uint vid : SV_VertexID)
{
	return FullscreenTriangleVS(vid);
}

float4 PSMain(FullscreenTriangleVSOutput IN) : SV_Target0
{
	float3 normalWS = CubemapFaceUvToDirection(g_Pass.CubemapFaceIndex, IN.UV);
	float3 color =
		IntegratePrefilteredSpecular(GetEnvironmentBinding(), normalWS, GetPerceptualRoughness());

	return float4(color, 1.0);
}
