#include <Common/BindlessResources.hlsli>
#include <Common/FullscreenTriangle.hlsli>
#include <PBR/BRDF.hlsli>

struct BrdfLutProbeParameters
{
	uint TextureIndex;
	uint SamplerIndex;
	uint2 Padding;
};

ConstantBuffer<BrdfLutProbeParameters> g_Pass : register(b2);

// These channels test representative dielectrics, a conductor, and the
// unit-reflectance furnace using the production compensation function.
static const float4 ProbeF0 = float4(0.04, 0.08, 0.7, 1.0);

float4 PSWhiteFurnace(FullscreenTriangleVSOutput input) : SV_Target0
{
	Texture2D<float2> lut = GetTexture2DFloat2(g_Pass.TextureIndex);
	SamplerState linearClamp = GetSamplerState(g_Pass.SamplerIndex);
	float2 brdf = lut.SampleLevel(linearClamp, saturate(input.UV), 0.0);
	float3 gain = GGXEnergyCompensation(ProbeF0.xyz, brdf);
	float unitGain = GGXEnergyCompensation(ProbeF0.www, brdf).x;
	return (ProbeF0 * brdf.x + brdf.y) * float4(gain, unitGain);
}
