#include <Common/ApplicationBinding.hlsli>
#include <Lighting/AtmosphereBinding.hlsli>
#include <Lighting/DirectionalShadowData.hlsli>

ConstantBuffer<DirectionalShadowData> g_Shadow : register(b3);

float4 VSMain(uint vertexId : SV_VertexID) : SV_POSITION
{
	float2 position = float2(vertexId == 2 ? 3.0 : -1.0, vertexId == 1 ? 3.0 : -1.0);
	return float4(position, 0.0, 1.0);
}

float4 PSMain() : SV_TARGET
{
	// Distinct values exercise both the first and final meaningful rows of each block.
	return float4((float)(g_Scene.ObjectCount) + g_Scene.IBLResource.EnvironmentIntensity,
		g_Shadow.SplitFar.x + g_Shadow.DistanceFadeInvRange,
		g_Atmosphere.Radii.x + g_Atmosphere.World.w,
		g_Views[g_Scene.ViewBaseIndex + g_Shadow.MainViewIndex].CameraPos.x);
}
