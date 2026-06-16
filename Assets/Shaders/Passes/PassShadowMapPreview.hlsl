#include <Common/Common.hlsli>
#include <Common/FullscreenTriangle.hlsli>
#include <Common/MaterialSampling.hlsli>
#include <Common/ApplicationBinding.hlsli>

uint GetShadowMapTextureIndex()
{
	return g_LocalParam0;
}

uint GetShadowMapSamplerIndex()
{
	return g_LocalParam1;
}

float GetPreviewMinDepth()
{
	return asfloat(g_LocalParam2);
}

float GetPreviewMaxDepth()
{
	return asfloat(g_LocalParam3);
}

bool IsPreviewInverted()
{
	return g_LocalParam4 != 0u;
}

FullscreenTriangleVSOutput VSMain(uint vertexId : SV_VertexID)
{
	return FullscreenTriangleVS(vertexId);
}

float SampleShadowMapDepth(float2 uv)
{
	Texture2D<float> shadowMap = GetTexture2DFloat(GetShadowMapTextureIndex());
	SamplerState shadowSampler = GetSamplerState(GetShadowMapSamplerIndex());
	return shadowMap.SampleLevel(shadowSampler, uv, 0.0);
}

float4 PSMain(FullscreenTriangleVSOutput IN) : SV_Target
{
	const float rawDepth = SampleShadowMapDepth(IN.UV);
	const float minDepth = saturate(GetPreviewMinDepth());
	const float maxDepth = max(saturate(GetPreviewMaxDepth()), minDepth + 1.0e-5);

	float depth = saturate((rawDepth - minDepth) / (maxDepth - minDepth));
	if (IsPreviewInverted())
	{
		depth = 1.0 - depth;
	}

	return float4(depth.xxx, 1.0);
}
