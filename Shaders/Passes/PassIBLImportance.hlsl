#include <Common/Common.hlsli>
#include <Common/FullscreenTriangle.hlsli>
#include <Common/Cubemap.hlsli>
#include <Common/MaterialSampling.hlsli>
#include <Lighting/IrradianceIntegrationMath.hlsli>

struct IBLImportancePassParameters
{
	uint CubemapFaceIndex;
	uint MipLevel;
	uint SourceTextureIndex;
	uint EnvironmentSamplerIndex;
	uint ImportanceResolution;
	float EnvironmentSourceMip;
	uint PhysicalSky;
	uint Padding;
};

ConstantBuffer<IBLImportancePassParameters> g_Pass : register(b2);

FullscreenTriangleVSOutput VSMain(uint vid : SV_VertexID)
{
	return FullscreenTriangleVS(vid);
}

float PSMain(FullscreenTriangleVSOutput IN) : SV_Target0
{
	const uint2 texel = (uint2)IN.PositionCS.xy;
	if (g_Pass.MipLevel == 0u)
	{
		const float3 direction = CubemapFaceUvToDirection(g_Pass.CubemapFaceIndex, IN.UV);
		float3 radiance = SampleTextureCubeLevel(g_Pass.SourceTextureIndex,
			g_Pass.EnvironmentSamplerIndex, direction, g_Pass.EnvironmentSourceMip).rgb;
		radiance = g_Pass.PhysicalSky ? SanitizeSceneRadiance(radiance) : SanitizeHDRColor(radiance);
		return dot(radiance, float3(0.2126, 0.7152, 0.0722)) *
			GetIrradianceTexelSolidAngle(texel.x, texel.y, g_Pass.ImportanceResolution);
	}
	Texture2DArray<float> source = GetTexture2DArrayFloat(g_Pass.SourceTextureIndex);
	const uint2 child = texel * 2u;
	// The source SRV exposes only the previous mip. Store sums, not averages.
	return source.Load(int4(child, g_Pass.CubemapFaceIndex, 0u)) +
		source.Load(int4(child + uint2(1u, 0u), g_Pass.CubemapFaceIndex, 0u)) +
		source.Load(int4(child + uint2(0u, 1u), g_Pass.CubemapFaceIndex, 0u)) +
		source.Load(int4(child + uint2(1u, 1u), g_Pass.CubemapFaceIndex, 0u));
}
