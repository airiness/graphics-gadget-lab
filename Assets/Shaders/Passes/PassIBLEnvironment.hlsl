#include <Common/Common.hlsli>
#include <Common/FullscreenTriangle.hlsli>
#include <Common/Cubemap.hlsli>
#include <Common/ApplicationBinding.hlsli>

#define g_CubemapFaceIndex g_DrawParam0

float3 ProcedualSkybox(float3 dir)
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

FullscreenTriangleVSOutput VSMain(uint vid : SV_VertexID)
{
	return FullscreenTriangleVS(vid);
}

float4 PSMain(FullscreenTriangleVSOutput IN) : SV_Target0
{
	float3 dir = CubemapFaceUvToDirection(g_CubemapFaceIndex, IN.UV);
	return float4(ProcedualSkybox(dir), 1.0);
}
