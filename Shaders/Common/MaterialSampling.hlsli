#pragma once
#include <Common/BindlessResources.hlsli>
#include <Common/BufferLayout.hlsli>

// Sample texture2D with srv and sampler indices
float4 SampleTexture2D(uint srvIndex, uint samplerIndex, float2 uv)
{
	Texture2D<float4> tex = GetTexture2DFloat4(srvIndex);
	SamplerState samp = GetSamplerState(samplerIndex);
	return tex.Sample(samp, uv);
}

// Sample texture2DLevel with srv and sampler indices
float4 SampleTexture2DLevel(uint srvIndex, uint samplerIndex, float2 uv, float lod)
{
	Texture2D<float4> tex = GetTexture2DFloat4(srvIndex);
	SamplerState samp = GetSamplerState(samplerIndex);
	return tex.SampleLevel(samp, uv, lod);
}

// Sample texture2D with srv index and sampler state
float4 SampleTexture2D(uint srvIndex, SamplerState samplerState, float2 uv)
{
	Texture2D<float4> tex = GetTexture2DFloat4(srvIndex);
	return tex.Sample(samplerState, uv);
}

float4 SampleTexture2DLevel(uint srvIndex, SamplerState samplerState, float2 uv, float lod)
{
	Texture2D<float4> tex = GetTexture2DFloat4(srvIndex);
	return tex.SampleLevel(samplerState, uv, lod);
}

float4 SampleTextureCube(uint srvIndex, uint samplerIndex, float3 direction)
{
	TextureCube<float4> tex = GetTextureCubeFloat4(srvIndex);
	SamplerState samp = GetSamplerState(samplerIndex);
	return tex.Sample(samp, direction);
}

float4 SampleTextureCubeLevel(uint srvIndex, uint samplerIndex, float3 direction, float lod)
{
	TextureCube<float4> tex = GetTextureCubeFloat4(srvIndex);
	SamplerState samp = GetSamplerState(samplerIndex);
	return tex.SampleLevel(samp, direction, lod);
}

float4 SampleTextureCube(uint srvIndex, SamplerState samplerState, float3 direction)
{
	TextureCube<float4> tex = GetTextureCubeFloat4(srvIndex);
	return tex.Sample(samplerState, direction);
}

float4 SampleTextureCubeLevel(uint srvIndex, SamplerState samplerState, float3 direction, float lod)
{
	TextureCube<float4> tex = GetTextureCubeFloat4(srvIndex);
	return tex.SampleLevel(samplerState, direction, lod);
}

// Sample texture2DLevel with binding data
float4 SampleTextureBinding(TextureSamplerBindingData bindingData, float2 uv)
{
	return SampleTexture2D(bindingData.TextureIndex, bindingData.SamplerIndex, uv);
}

// View-level LOD bias of material texture samples (ViewData.TextureLodBias), added to
// the asset-owned sampler bias. Pixel shader entry points that sample materials set it
// from their view before sampling; it stays zero elsewhere, for example in shadow
// views.
static float g_MaterialTextureLodBias = 0.0;

void SetMaterialTextureLodBias(float lodBias)
{
	g_MaterialTextureLodBias = lodBias;
}

// Material texture sample with the view-level LOD bias.
float4 SampleMaterialTextureBinding(TextureSamplerBindingData bindingData, float2 uv)
{
	Texture2D<float4> tex = GetTexture2DFloat4(bindingData.TextureIndex);
	SamplerState samp = GetSamplerState(bindingData.SamplerIndex);
	return tex.SampleBias(samp, uv, g_MaterialTextureLodBias);
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
	return SampleTextureCubeLevel(
		bindingData.TextureIndex, bindingData.SamplerIndex, direction, lod);
}

// Make TextureSamplerBindingData by uint2 binding data
TextureSamplerBindingData MakeTextureSamplerBinding(uint2 binding)
{
	TextureSamplerBindingData result;
	result.TextureIndex = binding.x;
	result.SamplerIndex = binding.y;
	return result;
}
