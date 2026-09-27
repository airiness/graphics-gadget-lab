#include <Common/Common.hlsli>
#include <Common/BindlessResources.hlsli>
#include <Common/BufferLayout.hlsli>
#include <Common/DepthReconstruction.hlsli>
#include <Common/FullscreenTriangle.hlsli>
#include <Lighting/Atmosphere.hlsli>

StructuredBuffer<ViewData> g_Views : register(t3);

struct PhysicalSkyPassParameters
{
	uint ViewIndex;
	uint SkyViewIndex;
	uint TransmittanceIndex;
	uint SamplerIndex;
	float3 SunDirection;
	float Padding;
};
ConstantBuffer<PhysicalSkyPassParameters> g_Pass : register(b2);

FullscreenTriangleVSOutput VSMain(uint vertexId : SV_VertexID)
{
	FullscreenTriangleVSOutput output = FullscreenTriangleVS(vertexId);
	output.PositionCS.z = GetDepthBackgroundValue(g_Views[g_Pass.ViewIndex].DepthConvention);
	return output;
}

float3 ReconstructWorldDirection(float2 uv, ViewData viewData)
{
	float2 ndc = uv * float2(2.0, -2.0) + float2(-1.0, 1.0);
	float4 farPositionVS = mul(float4(ndc, GetDepthFarValue(viewData.DepthConvention), 1.0),
		viewData.InvProjMat);
	float3 directionVS = SafeNormalize(farPositionVS.xyz / max(farPositionVS.w, 1.0e-6),
		float3(0.0, 0.0, 1.0));
	return SafeNormalize(mul(float4(directionVS, 0.0), viewData.InvViewMat).xyz,
		float3(0.0, 0.0, 1.0));
}

[earlydepthstencil]
float4 PSMain(FullscreenTriangleVSOutput input) : SV_Target0
{
	const ViewData viewData = g_Views[g_Pass.ViewIndex];
	const float3 direction = ReconstructWorldDirection(input.UV, viewData);
	const float3 observer = viewData.CameraPos.xyz * g_Atmosphere.World.w - g_Atmosphere.World.xyz;
	const float3 up = SafeNormalize(observer, float3(0.0, 1.0, 0.0));
	const float3 sun = SafeNormalize(g_Pass.SunDirection, float3(0.0, 1.0, 0.0));
	const float2 skyUV = AtmosphereSkyViewUV(direction, up, sun);
	Texture2D<float4> skyView = GetTexture2DFloat4(g_Pass.SkyViewIndex);
	Texture2D<float4> transmittance = GetTexture2DFloat4(g_Pass.TransmittanceIndex);
	SamplerState samplerState = GetSamplerState(g_Pass.SamplerIndex);
	float3 radiance = skyView.SampleLevel(samplerState, skyUV, 0).rgb;

	// The disk and direct BRDF share the same Y-normalized TOA illuminance and angular radius.
	const float sineRadius = sin(g_Atmosphere.Sun.w);
	const float projectedSolidAngle = AtmospherePi * sineRadius * sineRadius;
	const float cosine = dot(direction, sun);
	const float diskCoverage = saturate((cosine - cos(g_Atmosphere.Sun.w)) /
		max(fwidth(cosine), 1.0e-7) + 0.5);
	if (diskCoverage > 0.0)
	{
		radiance += diskCoverage * g_Atmosphere.Sun.rgb / projectedSolidAngle *
			AtmosphereSunTransmittance(transmittance, samplerState, observer, sun);
	}
	return float4(EncodeSceneColor(radiance, viewData.ScenePreExposure), 1.0);
}
