#include <Common/BindlessResources.hlsli>
#include <Lighting/Atmosphere.hlsli>
struct AtmospherePassParameters
{
	uint OutputIndex;
	uint TransmittanceIndex;
	uint MultipleScatteringIndex;
	uint SamplerIndex;
	uint Stage;
	uint Width;
	uint Height;
	uint Padding;
};
ConstantBuffer<AtmospherePassParameters> g_Pass : register(b2);

[numthreads(8,8,1)]
void CSMain(uint3 dispatchId : SV_DispatchThreadID)
{
	if (dispatchId.x>=g_Pass.Width || dispatchId.y>=g_Pass.Height) return;
	RWTexture2D<float4> output = GetRWTexture2DFloat4(g_Pass.OutputIndex);
	float2 uv = float2(dispatchId.xy)/float2(g_Pass.Width-1,g_Pass.Height-1);
	float radius = lerp(g_Atmosphere.Radii.x+0.001, g_Atmosphere.Radii.y-0.001, uv.y);
	float mu = 2.0*uv.x-1.0;
	float3 p = float3(0.0,radius,0.0);
	float3 d = float3(sqrt(saturate(1.0-mu*mu)),mu,0.0);
	if (g_Pass.Stage==0)
	{
		output[dispatchId.xy] = AtmosphereValidated(AtmosphereIntegrateTransmittance(p,d));
		return;
	}
	Texture2D<float4> transmittance = GetTexture2DFloat4(g_Pass.TransmittanceIndex);
	SamplerState linearSampler = GetSamplerState(g_Pass.SamplerIndex);
	if (g_Pass.Stage==1)
	{
		// Unit perpendicular solar illuminance. Integrate isotropic transport over a sphere.
		float3 sun = d;
		float3 meanSource = 0.0, meanTransfer = 0.0;
		for (uint sampleIndex=0; sampleIndex<64; ++sampleIndex)
		{
			float y = 1.0-2.0*(sampleIndex+0.5)/64.0;
			float phi = sampleIndex*2.39996322973;
			float sinTheta = sqrt(saturate(1.0-y*y));
			float3 rayDirection = float3(cos(phi)*sinTheta,y,sin(phi)*sinTheta);
			float ground = AtmosphereGroundDistance(p,rayDirection);
			float distance = ground>0.0 ? ground : AtmosphereTopDistance(p,rayDirection);
			float stepSize = distance/24.0;
			float3 throughput = 1.0, source = 0.0, transfer = 0.0;
			for (uint j=0; j<24; ++j)
			{
				float3 position = p+rayDirection*((j+0.5)*stepSize);
				float3 ray, extinction; float mie;
				AtmosphereMedium(position,ray,mie,extinction);
				float3 weight = throughput*AtmosphereSegmentIntegral(extinction,stepSize);
				float3 scatter = ray+mie;
				source += weight*scatter*AtmosphereSunTransmittance(transmittance,linearSampler,position,sun)/(4.0*AtmospherePi);
				transfer += weight*scatter;
				throughput *= exp(-extinction*stepSize);
			}
			if (ground>0.0)
			{
				float3 surface = p+rayDirection*ground;
				float3 normal = normalize(surface);
				surface += normal*0.001;
				source += throughput*g_Atmosphere.Ground.rgb*max(0.0,dot(normal,sun))*
					AtmosphereSunTransmittance(transmittance,linearSampler,surface,sun)/AtmospherePi;
				transfer += throughput*g_Atmosphere.Ground.rgb;
			}
			meanSource += source/64.0;
			meanTransfer += transfer/64.0;
		}
		// A bounded geometric-series closure; dense/high-albedo limits require separate quality review.
		output[dispatchId.xy] = AtmosphereValidated(meanSource/max(1.0-meanTransfer,0.001));
		return;
	}
	Texture2D<float4> multiple = GetTexture2DFloat4(g_Pass.MultipleScatteringIndex);
	radius = g_Atmosphere.Observer.x;
	p = float3(0.0,radius,0.0);
	float sunMu = g_Atmosphere.Observer.y;
	float3 sun = float3(sqrt(saturate(1.0-sunMu*sunMu)),sunMu,0.0);
	// Full sky latitude/longitude in a local radial frame whose +X points toward the sun azimuth.
	float zenith = uv.y*AtmospherePi;
	float azimuth = uv.x*2.0*AtmospherePi;
	d = float3(sin(zenith)*cos(azimuth),cos(zenith),sin(zenith)*sin(azimuth));
	float ground = AtmosphereGroundDistance(p,d);
	float distance = ground>0.0 ? ground : AtmosphereTopDistance(p,d);
	float stepSize = distance/40.0;
	float cosine = dot(d,sun);
	float rayPhase = 3.0*(1.0+cosine*cosine)/(16.0*AtmospherePi);
	float g = g_Atmosphere.Mie.w;
	float miePhase = (1.0-g*g)/(4.0*AtmospherePi*pow(max(0.001,1.0+g*g-2.0*g*cosine),1.5));
	float3 throughput = 1.0, radiance = 0.0;
	for (uint i=0; i<40; ++i)
	{
		float3 position = p+d*((i+0.5)*stepSize);
		float3 ray, extinction; float mie;
		AtmosphereMedium(position,ray,mie,extinction);
		float height = length(position);
		float3 multi = multiple.SampleLevel(linearSampler,
			AtmosphereLutUV(height,dot(position,sun)/height,float2(32,32)),0).rgb;
		float3 source = (ray*rayPhase+mie*miePhase)*AtmosphereSunTransmittance(transmittance,linearSampler,position,sun)
			+(ray+mie)*multi;
		radiance += throughput*source*AtmosphereSegmentIntegral(extinction,stepSize);
		throughput *= exp(-extinction*stepSize);
	}
	output[dispatchId.xy] = AtmosphereValidated(radiance*g_Atmosphere.Sun.rgb);
}
