#include <Common/ApplicationBinding.hlsli>
#include <Common/BindlessResources.hlsli>
#include <Common/FullscreenTriangle.hlsli>

// Writes the display-extent depth that the temporal resolve selected for each display
// pixel into the depth target of post-temporal composition.
struct TemporalAADisplayDepthPassParameters
{
	uint SourceDepthIndex;
	uint3 Padding;
};

ConstantBuffer<TemporalAADisplayDepthPassParameters> g_Pass : register(b2);

FullscreenTriangleVSOutput VSMain(uint vertexId : SV_VertexID)
{
	return FullscreenTriangleVS(vertexId);
}

float PSMain(FullscreenTriangleVSOutput IN) : SV_Depth
{
	Texture2D<float> sourceDepth = GetTexture2DFloat(g_Pass.SourceDepthIndex);
	return sourceDepth.Load(int3(uint2(IN.PositionCS.xy), 0));
}
