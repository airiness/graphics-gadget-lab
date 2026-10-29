#pragma once
#include <Common/EnvironmentSampling.hlsli>
#include <Lighting/GIRayScene.hlsli>
#include <Lighting/WorldSunTransmittance.hlsli>

float GIEnvironmentMip(uint filter)
{
	const uint2 binding = g_Scene.IBLResource.EnvironmentBinding;
	TextureCube<float4> environment = GetTextureCubeFloat4(binding.x);
	uint width;
	uint height;
	uint levels;
	environment.GetDimensions(0, width, height, levels);
	return filter == 1 ? 0.0 :
		min(max(log2(max((float)(width) / 32.0, 1.0)), 0.0), (float)(levels - 1));
}

float3 EvaluateGIMiss(float3 direction, bool constantEnvironment, float3 constantRadiance,
	float environmentMip)
{
	if (constantEnvironment)
	{
		return constantRadiance;
	}
	const uint2 binding = g_Scene.IBLResource.EnvironmentBinding;
	TextureCube<float4> environment = GetTextureCubeFloat4(binding.x);
	// Radiance cubemap, never irradiance/specular IBL or SceneColor. Mip zero is
	// a declared comparison signal; the production-compatible default is a 32-texel face.
	return environment.SampleLevel(GetSamplerState(binding.y),
		WorldToEnvironmentDirection(direction, g_Scene.IBLResource.EnvironmentRotationRadians),
		environmentMip).rgb * g_Scene.IBLResource.EnvironmentIntensity;
}

float3 EvaluateGIHitSun(GIHit hit, float maximumDistance, uint transmittance, uint samplerIndex)
{
	if (g_Scene.WorldSunLightIndex == 0xffffffffu)
	{
		return 0.0.xxx;
	}
	const LightData sun = g_Lights[g_Scene.WorldSunLightIndex];
	const float3 direction = normalize(-sun.Direction.xyz);
	const float cosine = saturate(dot(hit.VertexNormal, direction));
	if (cosine <= 0.0 || !GISunVisible(hit, direction, maximumDistance))
	{
		return 0.0.xxx;
	}
	// Solar-disc-centre visibility. No field feedback and no secondary IBL substitution.
	const float3 illuminance = sun.Color.rgb * sun.Intensity *
		WorldSunTransmittance(hit.Position, direction, transmittance, samplerIndex);
	return hit.Albedo * (cosine / 3.14159265358979323846) * illuminance;
}
