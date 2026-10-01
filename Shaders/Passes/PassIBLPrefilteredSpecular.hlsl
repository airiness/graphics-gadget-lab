#include <Common/Common.hlsli>
#include <Common/Sampling.hlsli>
#include <Common/FullscreenTriangle.hlsli>
#include <Common/Cubemap.hlsli>
#include <Common/MaterialSampling.hlsli>
#include <Common/ApplicationBinding.hlsli>
#include <PBR/BRDF.hlsli>
#include <Lighting/EnvironmentImportanceSampling.hlsli>

struct IBLPrefilteredSpecularPassParameters
{
	uint CubemapFaceIndex;
	uint MipLevel;
	uint MipLevels;
	uint EnvironmentTextureIndex;
	uint EnvironmentSamplerIndex;
	uint ImportanceTextureIndex;
	uint ImportanceResolution;
	uint SampleCount;
	float MaxSampleLuminance;
	uint PhysicalSky;
	uint2 Padding;
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
	if (g_Pass.PhysicalSky) return SanitizeSceneRadiance(radiance);
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
		float3 radiance = SampleTextureCubeLevel(environmentBinding, normalWS, 0.0).rgb;
		return g_Pass.PhysicalSky ? SanitizeSceneRadiance(radiance) : SanitizeHDRColor(radiance);
	}

	const uint sampleCount = max(g_Pass.SampleCount, 1u);
	Texture2DArray<float> importance = GetTexture2DArrayFloat(g_Pass.ImportanceTextureIndex);
	const float totalMass = GetEnvironmentImportanceMass(importance, g_Pass.ImportanceResolution);
	// An empty proposal can also result from mip quantization of a very dim HDR.
	// Retain GGX support instead of treating that proposal as proof of zero radiance.
	const uint environmentCount = totalMass > 0.0 ? sampleCount / 2u : 0u;
	const uint ggxCount = sampleCount - environmentCount;
	const float alpha = PerceptualRoughnessToAlpha(perceptualRoughness);
	float3 prefilteredColor = 0.0.xxx;
	float totalWeight = 0.0;

	for (uint i = 0; i < sampleCount; ++i)
	{
		float3 lightWS;
		float environmentPdf = 0.0;
		if (i < ggxCount)
		{
			float2 Xi = Hammersley(i, ggxCount);
			Xi.x += 0.5 / (float)ggxCount;
			const float3 halfWS = TangentToWorld(ImportanceSampleGGX(Xi, alpha), normalWS);
			lightWS = normalize(2.0 * dot(normalWS, halfWS) * halfWS - normalWS);
		}
		else
		{
			float2 Xi = Hammersley(i - ggxCount, environmentCount);
			Xi.x += 0.5 / (float)environmentCount;
			Xi.y += 0.5 / (float)environmentCount;
			lightWS = SampleEnvironmentImportance(importance, g_Pass.ImportanceResolution, totalMass, Xi, environmentPdf);
		}

		float NoL = saturate(dot(normalWS, lightWS));
		if (NoL > 0.0)
		{
			if (i < ggxCount && totalMass > 0.0)
			{
				environmentPdf = EvaluateEnvironmentImportancePdf(importance,
					g_Pass.ImportanceResolution, totalMass, lightWS);
			}
			const float ggxPdf = GetSpecularGGXPdf(NoL, alpha);
			const float weight = GetSpecularMISWeight(NoL, ggxPdf, environmentPdf, ggxCount, environmentCount);
			// Both proposals integrate the same radiance function. PDF-dependent
			// source mips followed by a luminance clamp change that target with sample density.
			const float3 sampleRadiance = SampleTextureCubeLevel(environmentBinding, lightWS, 0.0).rgb;
			prefilteredColor += ClampSampleLuminance(sampleRadiance) * weight;
			totalWeight += weight;
		}
	}

	// Hammersley includes the normal GGX sample, so totalWeight is positive.
	float3 result = prefilteredColor / totalWeight;
	return g_Pass.PhysicalSky ? SanitizeSceneRadiance(result) : SanitizeHDRColor(result);
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
