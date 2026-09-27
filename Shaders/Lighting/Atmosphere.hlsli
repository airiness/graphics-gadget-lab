#pragma once
// Original RGB atmosphere implementation. Lengths are km; coefficients are inverse km.
// LUT decomposition follows Hillaire (2020); the multiple-scattering closure is isotropic.
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
ConstantBuffer<AtmosphereParameters> g_Atmosphere : register(b0);
static const float AtmospherePi = 3.141592653589793;
float AtmosphereTopDistance(float3 p, float3 d)
{
	float b = dot(p, d);
	float radius = length(p);
	float c = (radius-g_Atmosphere.Radii.y)*(radius+g_Atmosphere.Radii.y);
	float root = sqrt(max(0.0,b*b-c));
	return max(0.0,b>=0.0 ? -c/max(b+root,1e-6) : -b+root);
}
float AtmosphereGroundDistance(float3 p, float3 d)
{
	float b = dot(p,d);
	float radius = length(p);
	float c = (radius-g_Atmosphere.Radii.x)*(radius+g_Atmosphere.Radii.x);
	float discriminant = b*b-c;
	float root = sqrt(max(0.0,discriminant));
	float t = b<0.0 ? c/max(-b+root,1e-6) : -1.0;
	return discriminant >= 0.0 && t > 0.0001 ? t : -1.0;
}
void AtmosphereMedium(float3 p, out float3 rayleigh, out float mie, out float3 extinction)
{
	float h = max(0.0, length(p)-g_Atmosphere.Radii.x);
	rayleigh = g_Atmosphere.Rayleigh.xyz * exp(-h/g_Atmosphere.Rayleigh.w);
	float densityMie = exp(-h/g_Atmosphere.Mie.z);
	mie = g_Atmosphere.Mie.x*densityMie;
	float ozone = saturate(1.0-abs(h-g_Atmosphere.Absorption.w)/g_Atmosphere.Ground.w);
	extinction = rayleigh + g_Atmosphere.Mie.y*densityMie + g_Atmosphere.Absorption.xyz*ozone;
}
float3 AtmosphereIntegrateTransmittance(float3 p, float3 d)
{
	if (AtmosphereGroundDistance(p,d) > 0.0) return 0.0;
	float stepSize = AtmosphereTopDistance(p,d)/64.0;
	float3 opticalDepth = 0.0;
	for (uint i=0; i<64; ++i)
	{
		float3 ray, extinction; float mie;
		AtmosphereMedium(p+d*((i+0.5)*stepSize), ray, mie, extinction);
		opticalDepth += extinction*stepSize;
	}
	return exp(-opticalDepth);
}
// Explicit texel-center mapping: x = zenith cosine, y = altitude, including endpoints.
float2 AtmosphereLutUV(float radius, float mu, float2 extent)
{
	float2 unitUV = float2(mu*0.5+0.5, (radius-g_Atmosphere.Radii.x)/(g_Atmosphere.Radii.y-g_Atmosphere.Radii.x));
	return (saturate(unitUV)*(extent-1.0)+0.5)/extent;
}
float3 AtmosphereSunTransmittance(Texture2D<float4> transmittance, SamplerState samplerState, float3 p, float3 sun)
{
	if (AtmosphereGroundDistance(p,sun)>0.0) return 0.0;
	float radius = length(p);
	return transmittance.SampleLevel(samplerState, AtmosphereLutUV(radius, dot(p,sun)/radius, float2(256,64)), 0).rgb;
}
float3 AtmosphereSegmentIntegral(float3 extinction, float stepSize)
{
	// Stable vacuum limit for channels with negligible extinction.
	return float3(extinction.x > 1e-6 ? (1.0-exp(-extinction.x*stepSize))/extinction.x : stepSize,
		extinction.y > 1e-6 ? (1.0-exp(-extinction.y*stepSize))/extinction.y : stepSize,
		extinction.z > 1e-6 ? (1.0-exp(-extinction.z*stepSize))/extinction.z : stepSize);
}
float4 AtmosphereValidated(float3 value)
{
	bool valid = all(isfinite(value)) && all(value >= 0.0);
	return valid ? float4(value,1.0) : float4(1.0,0.0,1.0,0.0);
}
