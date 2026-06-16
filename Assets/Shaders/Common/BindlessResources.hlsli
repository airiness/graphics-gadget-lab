#pragma once

Texture2D<float4> GetTexture2DFloat4(uint srvIndex)
{
	return ResourceDescriptorHeap[NonUniformResourceIndex(srvIndex)];
}

Texture2D<float> GetTexture2DFloat(uint srvIndex)
{
	return ResourceDescriptorHeap[NonUniformResourceIndex(srvIndex)];
}

TextureCube<float4> GetTextureCubeFloat4(uint srvIndex)
{
	return ResourceDescriptorHeap[NonUniformResourceIndex(srvIndex)];
}

SamplerState GetSamplerState(uint samplerIndex)
{
	return SamplerDescriptorHeap[NonUniformResourceIndex(samplerIndex)];
}

SamplerComparisonState GetSamplerComparisonState(uint samplerIndex)
{
	return SamplerDescriptorHeap[NonUniformResourceIndex(samplerIndex)];
}
