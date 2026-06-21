#include <Common/Common.hlsli>
#include <Common/FullscreenTriangle.hlsli>
#include <Common/MaterialSampling.hlsli>
#include <Common/ApplicationBinding.hlsli>

cbuffer ShadowMapPreviewPassConstants : register(b2)
{
	uint g_ShadowMapTextureIndex;
	uint g_ShadowMapSamplerIndex;
	float g_PreviewMinDepth;
	float g_PreviewMaxDepth;
	uint g_PreviewInvert;
	uint3 g_ShadowMapPreviewPassPadding;
};

bool IsPreviewInverted()
{
	return g_PreviewInvert != 0u;
}

FullscreenTriangleVSOutput VSMain(uint vertexId : SV_VertexID)
{
	return FullscreenTriangleVS(vertexId);
}

float SampleShadowMapDepth(float2 uv)
{
	Texture2D<float> shadowMap = GetTexture2DFloat(g_ShadowMapTextureIndex);
	SamplerState shadowSampler = GetSamplerState(g_ShadowMapSamplerIndex);
	return shadowMap.SampleLevel(shadowSampler, uv, 0.0);
}

float4 PSMain(FullscreenTriangleVSOutput IN) : SV_Target
{
	const float rawDepth = SampleShadowMapDepth(IN.UV);
	const float minDepth = saturate(g_PreviewMinDepth);
	const float maxDepth = max(saturate(g_PreviewMaxDepth), minDepth + 1.0e-5);

	float depth = saturate((rawDepth - minDepth) / (maxDepth - minDepth));
	if (IsPreviewInverted())
	{
		depth = 1.0 - depth;
	}

	return float4(depth.xxx, 1.0);
}
