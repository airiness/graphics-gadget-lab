#include <Common/Common.hlsli>
#include <Common/FullscreenTriangle.hlsli>
#include <Common/Cubemap.hlsli>
#include <Common/MaterialSampling.hlsli>

struct IBLEnvironmentMipPassParameters
{
	uint CubemapFaceIndex;
	uint SourceTextureIndex;
	uint SourceSamplerIndex;
	uint Padding;
};

ConstantBuffer<IBLEnvironmentMipPassParameters> g_Pass : register(b2);

FullscreenTriangleVSOutput VSMain(uint vid : SV_VertexID)
{
	return FullscreenTriangleVS(vid);
}

float4 PSMain(FullscreenTriangleVSOutput IN) : SV_Target0
{
	float3 direction = CubemapFaceUvToDirection(g_Pass.CubemapFaceIndex, IN.UV);
	return SampleTextureCubeLevel(
		g_Pass.SourceTextureIndex,
		g_Pass.SourceSamplerIndex,
		direction,
		0.0);
}
