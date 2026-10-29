#pragma once
#include <Common/BindlessResources.hlsli>
#include <Lighting/AtmosphereBinding.hlsli>

// Scene-referred attenuation shared by primary and secondary surface lighting.
float3 WorldSunTransmittance(float3 positionWS, float3 sunDirection,
	uint transmittanceIndex, uint samplerIndex)
{
	if (transmittanceIndex == 0xffffffffu)
	{
		return 1.0.xxx;
	}
	float3 positionKm = positionWS * g_Atmosphere.World.w - g_Atmosphere.World.xyz;
	float radiusKm = length(positionKm);
	float bottomKm = g_Atmosphere.Radii.x;
	float topKm = g_Atmosphere.Radii.y;
	float mu = dot(positionKm, sunDirection) / max(radiusKm, 1.0e-6);
	float b = radiusKm * mu;
	float c = (radiusKm - bottomKm) * (radiusKm + bottomKm);
	float discriminant = b * b - c;
	if (b < 0.0 && discriminant >= 0.0 && -b - sqrt(discriminant) > 0.0001)
	{
		return 0.0.xxx;
	}
	float2 unitUV = saturate(float2(mu * 0.5 + 0.5,
		(radiusKm - bottomKm) / max(topKm - bottomKm, 1.0e-6)));
	float2 uv = (unitUV * float2(255.0, 63.0) + 0.5) / float2(256.0, 64.0);
	return GetTexture2DFloat4(transmittanceIndex).SampleLevel(
		GetSamplerState(samplerIndex), uv, 0).rgb;
}
