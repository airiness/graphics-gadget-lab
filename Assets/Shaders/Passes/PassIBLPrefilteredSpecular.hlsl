#include <Common/Common.hlsli>
#include <Common/Sampling.hlsli>
#include <Common/FullscreenTriangle.hlsli>
#include <Common/Cubemap.hlsli>
#include <Common/MaterialSampling.hlsli>
#include <Common/ApplicationBinding.hlsli>
#include <PBR/BRDF.hlsli>

uint GetCubemapFaceIndex()
{
	return g_LocalParam0;
}

uint GetMipLevel()
{
	return g_LocalParam1;
}

uint GetMipLevels()
{
	return max(g_LocalParam2, 1u);
}

TextureSamplerBindingData GetEnvironmentBinding()
{
	return MakeTextureSamplerBinding(uint2(g_LocalParam3, g_LocalParam4));
}

float GetPerceptualRoughness()
{
	uint mipLevels = GetMipLevels();
	return mipLevels > 1u
		? (float) GetMipLevel() / (float) (mipLevels - 1u)
		: 0.0;
}

float3 IntegratePrefilteredSpecular(
	TextureSamplerBindingData environmentBinding,
	float3 normalWS,
	float perceptualRoughness)
{
	const uint SAMPLE_COUNT = 1024;

	float alpha = PerceptualRoughnessToAlpha(perceptualRoughness);
	float3 viewWS = normalWS;
	float3 prefilteredColor = 0.0.xxx;
	float totalWeight = 0.0;

	for (uint i = 0; i < SAMPLE_COUNT; ++i)
	{
		float2 Xi = Hammersley(i, SAMPLE_COUNT);
		float3 halfTS = ImportanceSampleGGX(Xi, alpha);
		float3 halfWS = TangentToWorld(halfTS, normalWS);
		float3 lightWS = normalize(2.0 * dot(viewWS, halfWS) * halfWS - viewWS);

		float NoL = saturate(dot(normalWS, lightWS));
		if (NoL > 0.0)
		{
			// Environment source mips are not generated yet. Sample mip 0 until
			// PDF-based source LOD selection is added with an environment mip chain.
			prefilteredColor += SampleTextureCubeLevel(environmentBinding, lightWS, 0.0).rgb * NoL;
			totalWeight += NoL;
		}
	}

	return prefilteredColor / max(totalWeight, 1e-5);
}

FullscreenTriangleVSOutput VSMain(uint vid : SV_VertexID)
{
	return FullscreenTriangleVS(vid);
}

float4 PSMain(FullscreenTriangleVSOutput IN) : SV_Target0
{
	float3 normalWS = CubemapFaceUvToDirection(GetCubemapFaceIndex(), IN.UV);
	float3 color = IntegratePrefilteredSpecular(
		GetEnvironmentBinding(),
		normalWS,
		GetPerceptualRoughness());

	return float4(color, 1.0);
}
