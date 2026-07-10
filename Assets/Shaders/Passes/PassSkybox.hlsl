#include <Common/Common.hlsli>
#include <Common/ApplicationBinding.hlsli>
#include <Common/EnvironmentSampling.hlsli>
#include <Common/FullscreenTriangle.hlsli>
#include <Common/MaterialSampling.hlsli>

struct SkyboxPassParameters
{
	uint ViewIndex;
	uint3 Padding;
};

ConstantBuffer<SkyboxPassParameters> g_Pass : register(b2);

FullscreenTriangleVSOutput VSMain(uint vertexId : SV_VertexID)
{
	return FullscreenTriangleVS(vertexId);
}

float3 ReconstructWorldDirection(float2 uv, ViewData viewData)
{
	float2 positionNdc = uv * float2(2.0, -2.0) + float2(-1.0, 1.0);
	float4 farPositionVS = mul(float4(positionNdc, 1.0, 1.0), viewData.InvProjMat);
	float3 directionVS = SafeNormalize(
		farPositionVS.xyz / max(farPositionVS.w, 1.0e-6),
		float3(0.0, 0.0, 1.0));
	return SafeNormalize(
		mul(float4(directionVS, 0.0), viewData.InvViewMat).xyz,
		float3(0.0, 0.0, 1.0));
}

float4 PSMain(FullscreenTriangleVSOutput IN) : SV_Target0
{
	const uint viewIndex = g_Scene.ViewBaseIndex + g_Pass.ViewIndex;
	const ViewData viewData = g_Views[viewIndex];
	float3 directionWS = ReconstructWorldDirection(IN.UV, viewData);
	float3 environmentDirection = WorldToEnvironmentDirection(
		directionWS,
		g_Scene.IBLResource.EnvironmentRotationRadians);

	TextureSamplerBindingData environmentBinding =
		MakeTextureSamplerBinding(g_Scene.IBLResource.EnvironmentBinding);
	float3 color = SampleTextureCubeLevel(
		environmentBinding,
		environmentDirection,
		0.0).rgb * g_Scene.IBLResource.EnvironmentIntensity;
	return float4(color, 1.0);
}
