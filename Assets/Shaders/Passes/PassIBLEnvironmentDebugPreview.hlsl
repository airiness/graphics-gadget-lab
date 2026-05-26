#include <Common/Common.hlsli>
#include <Common/FullscreenTriangle.hlsli>
#include <Common/Cubemap.hlsli>
#include <Common/MaterialSampling.hlsli>
#include <Common/ApplicationBinding.hlsli>

FullscreenTriangleVSOutput VSMain(uint vid : SV_VertexID)
{
	return FullscreenTriangleVS(vid);
}

float4 PSMain(FullscreenTriangleVSOutput IN) : SV_Target0
{
	TextureSamplerBindingData binding = MakeTextureSamplerBinding(g_Scene.IBLResource.EnvironmentBinding);

	float2 atlasCoord = saturate(IN.UV) * float2(3.0, 2.0);
	uint tileX = min((uint) atlasCoord.x, 2u);
	uint tileY = min((uint) atlasCoord.y, 1u);
	uint face = tileY * 3u + tileX;
	float2 faceUv = frac(atlasCoord);

	float3 dir = CubemapFaceUvToDirection(face, faceUv);
	float3 hdr = SampleTextureCube(binding, dir).rgb * g_Scene.IBLResource.EnvironmentIntensity;
	float3 color = LinearToSRGB(ACESFitted(hdr));
	return float4(color, 1.0);
}
