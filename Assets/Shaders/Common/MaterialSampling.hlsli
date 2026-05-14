#pragma once
#include <Common/BufferLayout.hlsli>

// Get texture by descriptor index
Texture2D<float4> GetTexture2D(uint srvIndex)
{
	return ResourceDescriptorHeap[NonUniformResourceIndex(srvIndex)];
}

// Get sampler by descriptor index
SamplerState GetSamplerState(uint samplerIndex)
{
	return SamplerDescriptorHeap[NonUniformResourceIndex(samplerIndex)];
}

// Sample texture2D with srv and sampler indices
float4 SampleTexture2D(uint srvIndex, uint samplerIndex, float2 uv)
{
	Texture2D<float4> tex = GetTexture2D(srvIndex);
	SamplerState samp = GetSamplerState(samplerIndex);
	return tex.Sample(samp, uv);
}

// Sample texture2DLevel with srv and sampler indices
float4 SampleTexture2DLevel(uint srvIndex, uint samplerIndex, float2 uv, float lod)
{
	Texture2D<float4> tex = GetTexture2D(srvIndex);
	SamplerState samp = GetSamplerState(samplerIndex);
	return tex.SampleLevel(samp, uv, lod);
}

// Sample texture2D with srv index and sampler state
float4 SampleTexture2D(uint srvIndex, SamplerState samplerState, float2 uv)
{
	Texture2D<float4> tex = GetTexture2D(srvIndex);
	return tex.Sample(samplerState, uv);
}

// Sample texture2DLevel with srv index and sampler state
float4 SampleTextureBinding(TextureBindingData binding, float2 uv)
{
	return SampleTexture2D(binding.TextureIndex, binding.SamplerIndex, uv);
}

// Sample texture2DLevel with srv index and sampler state
float4 SampleTextureBindingLevel(TextureBindingData binding, float2 uv, float lod)
{
	return SampleTexture2DLevel(binding.TextureIndex, binding.SamplerIndex, uv, lod);
}