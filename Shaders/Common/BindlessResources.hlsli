#pragma once

Texture2D<float4> GetTexture2DFloat4(uint srvIndex)
{
	return ResourceDescriptorHeap[NonUniformResourceIndex(srvIndex)];
}

Texture2D<float> GetTexture2DFloat(uint srvIndex)
{
	return ResourceDescriptorHeap[NonUniformResourceIndex(srvIndex)];
}

Texture2D<uint> GetTexture2DUint(uint srvIndex)
{
	return ResourceDescriptorHeap[NonUniformResourceIndex(srvIndex)];
}

TextureCube<float4> GetTextureCubeFloat4(uint srvIndex)
{
	return ResourceDescriptorHeap[NonUniformResourceIndex(srvIndex)];
}

RWTexture2D<uint> GetRWTexture2DUint(uint uavIndex)
{
	return ResourceDescriptorHeap[NonUniformResourceIndex(uavIndex)];
}

SamplerState GetSamplerState(uint samplerIndex)
{
	return SamplerDescriptorHeap[NonUniformResourceIndex(samplerIndex)];
}

SamplerComparisonState GetSamplerComparisonState(uint samplerIndex)
{
	return SamplerDescriptorHeap[NonUniformResourceIndex(samplerIndex)];
}
