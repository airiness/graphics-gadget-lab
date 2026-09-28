#include <Common/Common.hlsli>
#include <Common/FullscreenTriangle.hlsli>
#include <Common/Cubemap.hlsli>
#include <Common/ApplicationBinding.hlsli>
#include <Common/MaterialSampling.hlsli>
#include <Lighting/Atmosphere.hlsli>

struct IBLEnvironmentPassParameters
{
	uint CubemapFaceIndex;
	uint SourceTextureIndex;
	uint SourceSamplerIndex;
	uint SourceMode;
	float3 SunDirection;
	float Padding;
};

ConstantBuffer<IBLEnvironmentPassParameters> g_Pass : register(b2);

static const uint ENVIRONMENT_SOURCE_EQUIRECTANGULAR = 0;
static const uint ENVIRONMENT_SOURCE_CUBEMAP = 1;

float2 EquirectangularUvFromDirection(float3 dir)
{
	dir = normalize(dir);
	float longitude = atan2(dir.x, dir.z);
	float latitude = acos(clamp(dir.y, -1.0, 1.0));
	return float2(longitude / (2.0 * PI) + 0.5, latitude / PI);
}

float3 SampleEnvironmentSource(float3 dir)
{
	if (g_Pass.SourceMode == 2)
	{
		// Only atmospheric radiance: the calibrated direct solar disk is never indirect lighting.
		float2 skyUV = AtmosphereSkyViewUV(dir, float3(0,1,0), g_Pass.SunDirection);
		return SampleTexture2DLevel(g_Pass.SourceTextureIndex, g_Pass.SourceSamplerIndex, skyUV, 0).rgb;
	}
	if (g_Pass.SourceMode == ENVIRONMENT_SOURCE_CUBEMAP)
	{
		return SampleTextureCubeLevel(
			g_Pass.SourceTextureIndex, g_Pass.SourceSamplerIndex, dir, 0.0)
			.rgb;
	}

	float2 uv = EquirectangularUvFromDirection(dir);
	return SampleTexture2DLevel(g_Pass.SourceTextureIndex, g_Pass.SourceSamplerIndex, uv, 0.0).rgb;
}

FullscreenTriangleVSOutput VSMain(uint vid : SV_VertexID)
{
	return FullscreenTriangleVS(vid);
}

float4 PSMain(FullscreenTriangleVSOutput IN) : SV_Target0
{
	float3 dir = CubemapFaceUvToDirection(g_Pass.CubemapFaceIndex, IN.UV);
	float3 radiance = SampleEnvironmentSource(dir);
	return float4(g_Pass.SourceMode == 2 ? SanitizeSceneRadiance(radiance) : SanitizeHDRColor(radiance), 1.0);
}
