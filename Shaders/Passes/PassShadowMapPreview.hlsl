#include <Common/Common.hlsli>
#include <Common/FullscreenTriangle.hlsli>
#include <Common/MaterialSampling.hlsli>
#include <Common/ApplicationBinding.hlsli>

struct ShadowMapPreviewPassParameters
{
	uint ShadowMapTextureIndex;
	uint ShadowMapSamplerIndex;
	float PreviewMinDepth;
	float PreviewMaxDepth;
	uint PreviewInvert;
	uint CascadeIndex;
	uint2 Padding;
};

ConstantBuffer<ShadowMapPreviewPassParameters> g_Pass : register(b2);

bool IsPreviewInverted()
{
	return g_Pass.PreviewInvert != 0u;
}

FullscreenTriangleVSOutput VSMain(uint vertexId : SV_VertexID)
{
	return FullscreenTriangleVS(vertexId);
}

float SampleShadowMapDepth(float2 uv)
{
	Texture2DArray<float> shadowMap = GetTexture2DArrayFloat(g_Pass.ShadowMapTextureIndex);
	SamplerState shadowSampler = GetSamplerState(g_Pass.ShadowMapSamplerIndex);
	return shadowMap.SampleLevel(shadowSampler, float3(uv, g_Pass.CascadeIndex), 0.0);
}

float4 PSMain(FullscreenTriangleVSOutput IN) : SV_Target
{
	const float rawDepth = SampleShadowMapDepth(IN.UV);
	const float minDepth = saturate(g_Pass.PreviewMinDepth);
	const float maxDepth = max(saturate(g_Pass.PreviewMaxDepth), minDepth + 1.0e-5);

	float depth = saturate((rawDepth - minDepth) / (maxDepth - minDepth));
	if (IsPreviewInverted())
	{
		depth = 1.0 - depth;
	}

	return float4(depth.xxx, 1.0);
}
