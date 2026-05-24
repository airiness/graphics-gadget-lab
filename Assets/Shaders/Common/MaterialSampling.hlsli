#pragma once
#include <Common/BufferLayout.hlsli>

// Get texture by descriptor index
Texture2D<float4> GetTexture2D(uint srvIndex)
{
	return ResourceDescriptorHeap[NonUniformResourceIndex(srvIndex)];
}

TextureCube<float4> GetTextureCube(uint srvIndex)
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

float4 SampleTexture2DLevel(uint srvIndex, SamplerState samplerState, float2 uv, float lod)
{
	Texture2D<float4> tex = GetTexture2D(srvIndex);
	return tex.SampleLevel(samplerState, uv, lod);
}

float4 SampleTextureCube(uint srvIndex, uint samplerIndex, float3 direction)
{
	TextureCube<float4> tex = GetTextureCube(srvIndex);
	SamplerState samp = GetSamplerState(samplerIndex);
	return tex.Sample(samp, direction);
}

float4 SampleTextureCubeLevel(uint srvIndex, uint samplerIndex, float3 direction, float lod)
{
	TextureCube<float4> tex = GetTextureCube(srvIndex);
	SamplerState samp = GetSamplerState(samplerIndex);
	return tex.SampleLevel(samp, direction, lod);
}

float4 SampleTextureCube(uint srvIndex, SamplerState samplerState, float3 direction)
{
	TextureCube<float4> tex = GetTextureCube(srvIndex);
	return tex.Sample(samplerState, direction);
}

float4 SampleTextureCubeLevel(uint srvIndex, SamplerState samplerState, float3 direction, float lod)
{
	TextureCube<float4> tex = GetTextureCube(srvIndex);
	return tex.SampleLevel(samplerState, direction, lod);
}

// Sample texture2DLevel with binding data
float4 SampleTextureBinding(TextureSamplerBindingData bindingData, float2 uv)
{
	return SampleTexture2D(bindingData.TextureIndex, bindingData.SamplerIndex, uv);
}

// Sample texture2DLevel with binding data
float4 SampleTextureBindingLevel(TextureSamplerBindingData bindingData, float2 uv, float lod)
{
	return SampleTexture2DLevel(bindingData.TextureIndex, bindingData.SamplerIndex, uv, lod);
}

// Sample textureCube with binding data
float4 SampleTextureCube(TextureSamplerBindingData bindingData, float3 direction)
{
	return SampleTextureCube(bindingData.TextureIndex, bindingData.SamplerIndex, direction);
}

// Sample textureCubeLevel with binding data
float4 SampleTextureCubeLevel(TextureSamplerBindingData bindingData, float3 direction, float lod)
{
	return SampleTextureCubeLevel(bindingData.TextureIndex, bindingData.SamplerIndex, direction, lod);
}

// Make TextureSamplerBindingData by uint2 binding data
TextureSamplerBindingData MakeTextureSamplerBinding(uint2 binding)
{
	TextureSamplerBindingData result;
	result.TextureIndex = binding.x;
	result.SamplerIndex = binding.y;
	return result;
}
