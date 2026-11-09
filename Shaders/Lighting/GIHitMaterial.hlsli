#pragma once
#include <Common/MaterialUtils.hlsli>

// GI 1.0 uses a ray-count-independent footprint: the first mip no larger than 64 texels.
uint GIHitTextureLevel(uint width, uint height, uint levels)
{
	uint level = 0;
	uint longest = max(width, height);
	while (longest > 64 && level + 1 < levels)
	{
		longest = max(longest >> 1, 1u);
		++level;
	}
	return level;
}

float4 SampleGIHitTexture(MaterialTextureBindingData binding, float2 uv0, float2 uv1)
{
	if (binding.TextureEnabled == 0)
	{
		return 1.0.xxxx;
	}
	Texture2D<float4> texture = GetTexture2DFloat4(binding.TextureSamplerBinding.TextureIndex);
	SamplerState samplerState = GetSamplerState(binding.TextureSamplerBinding.SamplerIndex);
	uint width, height, levels;
	texture.GetDimensions(0, width, height, levels);
	return texture.SampleLevel(samplerState, SelectUV(binding, uv0, uv1),
		GIHitTextureLevel(width, height, levels));
}

float3 EvaluateGIHitAlbedo(MaterialData material, float2 uv0, float2 uv1)
{
	const float3 baseColor = material.BaseColorFactor.rgb * SampleGIHitTexture(material.BaseColorBinding, uv0, uv1).rgb;
	const float metallic = saturate(material.MetallicFactor * SampleGIHitTexture(material.MetallicRoughnessBinding, uv0, uv1).b);
	return baseColor * (1.0 - metallic);
}
