#include <Common/Common.hlsli>
#include <Common/FullscreenTriangle.hlsli>
#include <Common/Cubemap.hlsli>
#include <Common/ApplicationBinding.hlsli>
#include <Common/MaterialSampling.hlsli>

struct IBLEnvironmentPassParameters
{
	uint CubemapFaceIndex;
	uint SourceTextureIndex;
	uint SourceSamplerIndex;
	uint SourceMode;
};

ConstantBuffer<IBLEnvironmentPassParameters> g_Pass : register(b2);

float3 ProceduralSkybox(float3 dir)
{
	float t = saturate(dir.y * 0.5 + 0.5);

	float3 ground = float3(0.04, 0.035, 0.03);
	float3 skyHorizon = float3(0.45, 0.55, 0.75);
	float3 skyZenith = float3(0.08, 0.18, 0.45);

	float3 sky = lerp(skyHorizon, skyZenith, pow(t, 1.5));
	float3 color = lerp(ground, sky, t);

	float3 sunDir = normalize(float3(0.2, 0.8, 0.3));
	float sun = pow(saturate(dot(dir, sunDir)), 512.0);
	color += sun * float3(8.0, 6.5, 4.0);

	return color;
}

float2 EquirectangularUvFromDirection(float3 dir)
{
	dir = normalize(dir);
	float longitude = atan2(dir.x, dir.z);
	float latitude = acos(clamp(dir.y, -1.0, 1.0));
	return float2(longitude / (2.0 * PI) + 0.5, latitude / PI);
}

float3 SampleEnvironmentSource(float3 dir)
{
	if (g_Pass.SourceMode == 0u)
	{
		return ProceduralSkybox(dir);
	}

	float2 uv = EquirectangularUvFromDirection(dir);
	return SampleTexture2DLevel(
		g_Pass.SourceTextureIndex,
		g_Pass.SourceSamplerIndex,
		uv,
		0.0).rgb;
}

FullscreenTriangleVSOutput VSMain(uint vid : SV_VertexID)
{
	return FullscreenTriangleVS(vid);
}

float4 PSMain(FullscreenTriangleVSOutput IN) : SV_Target0
{
	float3 dir = CubemapFaceUvToDirection(g_Pass.CubemapFaceIndex, IN.UV);
	return float4(SampleEnvironmentSource(dir), 1.0);
}
