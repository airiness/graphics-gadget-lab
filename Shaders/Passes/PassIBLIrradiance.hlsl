#include <Common/Common.hlsli>
#include <Common/FullscreenTriangle.hlsli>
#include <Common/Cubemap.hlsli>
#include <Common/MaterialSampling.hlsli>
#include <Common/ApplicationBinding.hlsli>
#include <Lighting/IrradianceIntegrationMath.hlsli>

struct IBLIrradiancePassParameters
{
	uint CubemapFaceIndex;
	uint EnvironmentTextureIndex;
	uint EnvironmentSamplerIndex;
	uint EnvironmentResolution;
	uint EnvironmentMipLevels;
	uint SampleCount;
	uint PhysicalSky;
	uint Padding;
};

ConstantBuffer<IBLIrradiancePassParameters> g_Pass : register(b2);

TextureSamplerBindingData GetEnvironmentBinding()
{
	return MakeTextureSamplerBinding(
		uint2(g_Pass.EnvironmentTextureIndex, g_Pass.EnvironmentSamplerIndex));
}

float3 IntegrateIrradiance(TextureSamplerBindingData environmentBinding, float3 normalWS)
{
	const uint sourceMip = GetIrradianceSourceMip(
		g_Pass.SampleCount, g_Pass.EnvironmentResolution, g_Pass.EnvironmentMipLevels);
	const uint resolution = g_Pass.EnvironmentResolution >> sourceMip;
	float3 irradiance = 0.0.xxx;
	float cosineIntegral = 0.0;

	// Keep environment directions fixed across output normals. Rotating a sparse
	// hemisphere pattern makes small HDR lights appear as spatial irradiance patches.
	[loop]
	for (uint y = 0u; y < resolution; ++y)
	{
		[loop]
		for (uint x = 0u; x < resolution; ++x)
		{
			const float2 uv = (float2(x, y) + 0.5) / (float)resolution;
			const float solidAngle = GetIrradianceTexelSolidAngle(x, y, resolution);
			[unroll]
			for (uint face = 0u; face < CUBEMAP_FACE_COUNT; ++face)
			{
				const float3 directionWS = CubemapFaceUvToDirection(face, uv);
				const float weight = saturate(dot(normalWS, directionWS)) * solidAngle;
				if (weight > 0.0)
				{
					float3 radiance = SampleTextureCubeLevel(
						environmentBinding, directionWS, (float)sourceMip).rgb;
					radiance = g_Pass.PhysicalSky
						? SanitizeSceneRadiance(radiance) : SanitizeHDRColor(radiance);
					irradiance += radiance * weight;
					cosineIntegral += weight;
				}
			}
		}
	}

	float3 result = irradiance * GetIrradianceNormalization(cosineIntegral);
	return g_Pass.PhysicalSky ? SanitizeSceneRadiance(result) : SanitizeHDRColor(result);
}

FullscreenTriangleVSOutput VSMain(uint vid : SV_VertexID)
{
	return FullscreenTriangleVS(vid);
}

float4 PSMain(FullscreenTriangleVSOutput IN) : SV_Target0
{
	float3 normalWS = CubemapFaceUvToDirection(g_Pass.CubemapFaceIndex, IN.UV);
	TextureSamplerBindingData environmentBinding = GetEnvironmentBinding();

	return float4(IntegrateIrradiance(environmentBinding, normalWS), 1.0);
}
