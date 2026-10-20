#pragma once
struct AtmosphereParameters
{
	float4 Radii;
	float4 Rayleigh;
	float4 Mie;
	float4 Absorption;
	float4 Ground;
	float4 Sun;
	float4 Observer;
	float4 World;
};
ConstantBuffer<AtmosphereParameters> g_Atmosphere : register(b5);
