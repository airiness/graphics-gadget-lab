#include <Common/BindlessResources.hlsli>
#include <Common/DepthReconstruction.hlsli>

static const uint FORWARD_PLUS_VALIDATION_TILE_SIZE = 16u;
static const uint FORWARD_PLUS_VALIDATION_GROUP_SIZE = 256u;
static const uint INVALID_PIXEL_INDEX = 0xffffffffu;

struct HdrDiffParameters
{
	uint SceneColorTextureIndex;
	uint LegacyReferenceTextureIndex;
	uint DepthTextureIndex;
	uint Width;
	uint Height;
	uint DepthConvention;
	uint TileCountX;
	uint TileCount;
};

ConstantBuffer<HdrDiffParameters> g_Pass : register(b0);
#if defined(GGLAB_FORWARD_PLUS_VALIDATION_REDUCE_FRAME)
StructuredBuffer<uint4> g_TileMetrics : register(t0);
#endif
RWStructuredBuffer<uint4> g_OutputMetrics : register(u0);

groupshared uint g_MaxAbsoluteError[FORWARD_PLUS_VALIDATION_GROUP_SIZE];
groupshared uint g_MaxRelativeLuminanceError[FORWARD_PLUS_VALIDATION_GROUP_SIZE];
groupshared uint g_MaxErrorPixel[FORWARD_PLUS_VALIDATION_GROUP_SIZE];
groupshared uint g_ComparedPixelCount[FORWARD_PLUS_VALIDATION_GROUP_SIZE];

void ReduceGroup(uint groupThreadIndex)
{
	for (uint stride = FORWARD_PLUS_VALIDATION_GROUP_SIZE / 2u; stride > 0u; stride >>= 1u)
	{
		GroupMemoryBarrierWithGroupSync();
		if (groupThreadIndex < stride)
		{
			const uint otherIndex = groupThreadIndex + stride;
			const uint otherAbsoluteError = g_MaxAbsoluteError[otherIndex];
			const uint otherPixel = g_MaxErrorPixel[otherIndex];
			if (otherAbsoluteError > g_MaxAbsoluteError[groupThreadIndex] ||
				(otherAbsoluteError == g_MaxAbsoluteError[groupThreadIndex] &&
				 otherPixel < g_MaxErrorPixel[groupThreadIndex]))
			{
				g_MaxAbsoluteError[groupThreadIndex] = otherAbsoluteError;
				g_MaxErrorPixel[groupThreadIndex] = otherPixel;
			}
			g_MaxRelativeLuminanceError[groupThreadIndex] = max(
				g_MaxRelativeLuminanceError[groupThreadIndex],
				g_MaxRelativeLuminanceError[otherIndex]);
			g_ComparedPixelCount[groupThreadIndex] += g_ComparedPixelCount[otherIndex];
		}
	}
	GroupMemoryBarrierWithGroupSync();
}

#if defined(GGLAB_FORWARD_PLUS_VALIDATION_REDUCE_TILES)
[numthreads(FORWARD_PLUS_VALIDATION_TILE_SIZE, FORWARD_PLUS_VALIDATION_TILE_SIZE, 1)]
void CSReduceTiles(
	uint3 groupId : SV_GroupID, uint3 groupThreadId : SV_GroupThreadID,
	uint groupThreadIndex : SV_GroupIndex)
{
	const uint2 pixel =
		groupId.xy * FORWARD_PLUS_VALIDATION_TILE_SIZE + groupThreadId.xy;
	uint absoluteErrorBits = 0u;
	uint relativeLuminanceErrorBits = 0u;
	uint maxErrorPixel = INVALID_PIXEL_INDEX;
	uint comparedPixelCount = 0u;

	if (all(pixel < uint2(g_Pass.Width, g_Pass.Height)))
	{
		Texture2D<float> depthTexture = GetTexture2DFloat(g_Pass.DepthTextureIndex);
		const float rawDepth = depthTexture.Load(int3(pixel, 0));
		if (!IsDepthBackground(rawDepth, g_Pass.DepthConvention))
		{
			Texture2D<float4> sceneColor = GetTexture2DFloat4(g_Pass.SceneColorTextureIndex);
			Texture2D<float4> legacyReference =
				GetTexture2DFloat4(g_Pass.LegacyReferenceTextureIndex);
			const float3 forwardPlus = sceneColor.Load(int3(pixel, 0)).rgb;
			const float3 legacy = legacyReference.Load(int3(pixel, 0)).rgb;

			float absoluteError = 3.402823466e+38;
			float relativeLuminanceError = 3.402823466e+38;
			if (all(isfinite(forwardPlus)) && all(isfinite(legacy)))
			{
				absoluteError = max(max(abs(forwardPlus.r - legacy.r),
					abs(forwardPlus.g - legacy.g)), abs(forwardPlus.b - legacy.b));
				const float3 luminanceWeights = float3(0.2126, 0.7152, 0.0722);
				const float forwardPlusLuminance = dot(forwardPlus, luminanceWeights);
				const float legacyLuminance = dot(legacy, luminanceWeights);
				relativeLuminanceError = abs(forwardPlusLuminance - legacyLuminance) /
					max(abs(legacyLuminance), 1.0e-4);
			}

			absoluteErrorBits = asuint(max(absoluteError, 0.0));
			relativeLuminanceErrorBits = asuint(max(relativeLuminanceError, 0.0));
			maxErrorPixel = pixel.y * g_Pass.Width + pixel.x;
			comparedPixelCount = 1u;
		}
	}

	g_MaxAbsoluteError[groupThreadIndex] = absoluteErrorBits;
	g_MaxRelativeLuminanceError[groupThreadIndex] = relativeLuminanceErrorBits;
	g_MaxErrorPixel[groupThreadIndex] = maxErrorPixel;
	g_ComparedPixelCount[groupThreadIndex] = comparedPixelCount;
	ReduceGroup(groupThreadIndex);

	if (groupThreadIndex == 0u)
	{
		const uint tileIndex = groupId.y * g_Pass.TileCountX + groupId.x;
		g_OutputMetrics[tileIndex] = uint4(g_MaxAbsoluteError[0],
			g_MaxRelativeLuminanceError[0], g_MaxErrorPixel[0], g_ComparedPixelCount[0]);
	}
}
#endif

#if defined(GGLAB_FORWARD_PLUS_VALIDATION_REDUCE_FRAME)
[numthreads(FORWARD_PLUS_VALIDATION_GROUP_SIZE, 1, 1)]
void CSReduceFrame(uint groupThreadIndex : SV_GroupIndex)
{
	uint absoluteErrorBits = 0u;
	uint relativeLuminanceErrorBits = 0u;
	uint maxErrorPixel = INVALID_PIXEL_INDEX;
	uint comparedPixelCount = 0u;

	for (uint tileIndex = groupThreadIndex; tileIndex < g_Pass.TileCount;
		tileIndex += FORWARD_PLUS_VALIDATION_GROUP_SIZE)
	{
		const uint4 tileMetrics = g_TileMetrics[tileIndex];
		if (tileMetrics.x > absoluteErrorBits ||
			(tileMetrics.x == absoluteErrorBits && tileMetrics.z < maxErrorPixel))
		{
			absoluteErrorBits = tileMetrics.x;
			maxErrorPixel = tileMetrics.z;
		}
		relativeLuminanceErrorBits = max(relativeLuminanceErrorBits, tileMetrics.y);
		comparedPixelCount += tileMetrics.w;
	}

	g_MaxAbsoluteError[groupThreadIndex] = absoluteErrorBits;
	g_MaxRelativeLuminanceError[groupThreadIndex] = relativeLuminanceErrorBits;
	g_MaxErrorPixel[groupThreadIndex] = maxErrorPixel;
	g_ComparedPixelCount[groupThreadIndex] = comparedPixelCount;
	ReduceGroup(groupThreadIndex);

	if (groupThreadIndex == 0u)
	{
		g_OutputMetrics[0] = uint4(g_MaxAbsoluteError[0],
			g_MaxRelativeLuminanceError[0], g_MaxErrorPixel[0], g_ComparedPixelCount[0]);
	}
}
#endif
