#include <Common/ApplicationBinding.hlsli>
#include <Common/BindlessResources.hlsli>

// Copies the render-domain scene depth into the next Temporal AA depth history. The next
// frame validates its reprojection against these samples, which stay at the render
// extent the scene was rasterized at.
struct TemporalAADepthHistoryPassParameters
{
	uint CurrentDepthIndex;
	uint NextHistoryDepthUavIndex;
	uint2 Padding;
};

ConstantBuffer<TemporalAADepthHistoryPassParameters> g_Pass : register(b2);

[numthreads(8, 8, 1)]
void CSMain(uint3 dispatchThreadId : SV_DispatchThreadID)
{
	Texture2D<float> currentDepthTexture = GetTexture2DFloat(g_Pass.CurrentDepthIndex);
	uint width;
	uint height;
	currentDepthTexture.GetDimensions(width, height);
	const uint2 pixel = dispatchThreadId.xy;
	if (any(pixel >= uint2(width, height)))
	{
		return;
	}

	RWTexture2D<float> nextHistoryDepth =
		GetRWTexture2DFloat(g_Pass.NextHistoryDepthUavIndex);
	nextHistoryDepth[pixel] = currentDepthTexture.Load(int3(pixel, 0));
}
