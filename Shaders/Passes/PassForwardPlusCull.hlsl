#include <Common/BindlessResources.hlsli>
#include <Common/BufferLayout.hlsli>
#include <Common/DepthReconstruction.hlsli>
#include <Lighting/ForwardPlus.hlsli>

struct ForwardPlusCullParameters
{
	uint DepthTextureIndex;
	uint ViewIndex;
	uint LightBaseIndex;
	uint LightCount;
	uint2 Extent;
	uint2 TileCount;
};

ConstantBuffer<ForwardPlusCullParameters> g_Pass :
	register(b0);
StructuredBuffer<ViewData> g_ForwardPlusViews :
	register(t0);
StructuredBuffer<LightData> g_ForwardPlusLights :
	register(t1);
RWStructuredBuffer<uint2> g_TileLightHeaders :
	register(u0);
RWStructuredBuffer<uint> g_TileLightIndices :
	register(u1);

groupshared uint g_MinViewZBits;
groupshared uint g_MaxViewZBits;
groupshared uint g_ValidDepthCount;
groupshared uint g_LightHits[
	FORWARD_PLUS_TILE_LIGHT_CAPACITY];

float3 ReconstructTileRay(
	float2 uv,
	float4x4 inverseProjection)
{
	const float2 ndc = UVToNDC(uv);
	const float4 viewPosition = mul(
		float4(ndc, 1.0, 1.0),
		inverseProjection);
	if (!all(isfinite(viewPosition)) ||
		abs(viewPosition.w) <= 1.0e-8)
	{
		return float3(ndc, 1.0);
	}
	return viewPosition.xyz / viewPosition.w;
}

bool IntersectsTileFrustum(
	float3 centerVS,
	float radius,
	float minViewZ,
	float maxViewZ,
	float3 leftPlane,
	float3 rightPlane,
	float3 topPlane,
	float3 bottomPlane)
{
	if (centerVS.z + radius < minViewZ ||
		centerVS.z - radius > maxViewZ ||
		centerVS.z + radius <= 0.0)
	{
		return false;
	}

	return dot(leftPlane, centerVS) >= -radius &&
		dot(rightPlane, centerVS) >= -radius &&
		dot(topPlane, centerVS) >= -radius &&
		dot(bottomPlane, centerVS) >= -radius;
}

[numthreads(
	FORWARD_PLUS_TILE_SIZE,
	FORWARD_PLUS_TILE_SIZE,
	1)]
void CSMain(
	uint3 groupId : SV_GroupID,
	uint3 groupThreadId : SV_GroupThreadID,
	uint groupThreadIndex : SV_GroupIndex)
{
	if (groupThreadIndex == 0)
	{
		g_MinViewZBits = asuint(3.402823466e+38);
		g_MaxViewZBits = 0u;
		g_ValidDepthCount = 0u;
	}
	if (groupThreadIndex <
		FORWARD_PLUS_TILE_LIGHT_CAPACITY)
	{
		g_LightHits[groupThreadIndex] = 0u;
	}
	GroupMemoryBarrierWithGroupSync();

	const ViewData viewData =
		g_ForwardPlusViews[g_Pass.ViewIndex];
	const uint2 pixel =
		groupId.xy * FORWARD_PLUS_TILE_SIZE +
		groupThreadId.xy;
	if (all(pixel < g_Pass.Extent))
	{
		Texture2D<float> depthTexture =
			GetTexture2DFloat(
				g_Pass.DepthTextureIndex);
		const float rawDepth = depthTexture.Load(
			int3(pixel, 0));
		if (!IsDepthBackground(
				rawDepth,
				viewData.DepthConvention))
		{
			const float viewZ =
				RawDepthToPositiveViewZ(
					PixelCenterToUV(
						pixel,
						g_Pass.Extent),
					rawDepth,
					viewData.InvProjMat);
			if (isfinite(viewZ) && viewZ > 0.0)
			{
				InterlockedMin(
					g_MinViewZBits,
					asuint(viewZ));
				InterlockedMax(
					g_MaxViewZBits,
					asuint(viewZ));
				InterlockedAdd(
					g_ValidDepthCount,
					1u);
			}
		}
	}
	GroupMemoryBarrierWithGroupSync();

	const uint tileIndex =
		groupId.y * g_Pass.TileCount.x +
		groupId.x;
	const uint tileOffset =
		tileIndex *
		FORWARD_PLUS_TILE_LIGHT_CAPACITY;
	if (g_ValidDepthCount == 0u)
	{
		if (groupThreadIndex == 0)
		{
			g_TileLightHeaders[tileIndex] =
				uint2(tileOffset, 0u);
		}
		return;
	}

	const float minViewZ = asfloat(
		g_MinViewZBits);
	const float maxViewZ = asfloat(
		g_MaxViewZBits);
	const float2 tilePixelMin =
		float2(groupId.xy *
			FORWARD_PLUS_TILE_SIZE);
	const float2 tilePixelMax =
		float2(min(
			(groupId.xy + 1u) *
				FORWARD_PLUS_TILE_SIZE,
			g_Pass.Extent));
	const float2 inverseExtent =
		1.0 / max(
			float2(g_Pass.Extent),
			float2(1.0, 1.0));
	const float2 uvMin =
		tilePixelMin * inverseExtent;
	const float2 uvMax =
		tilePixelMax * inverseExtent;

	const float3 topLeft = ReconstructTileRay(
		float2(uvMin.x, uvMin.y),
		viewData.InvProjMat);
	const float3 topRight = ReconstructTileRay(
		float2(uvMax.x, uvMin.y),
		viewData.InvProjMat);
	const float3 bottomLeft = ReconstructTileRay(
		float2(uvMin.x, uvMax.y),
		viewData.InvProjMat);
	const float3 bottomRight = ReconstructTileRay(
		float2(uvMax.x, uvMax.y),
		viewData.InvProjMat);
	const float3 leftPlane =
		normalize(cross(topLeft, bottomLeft));
	const float3 rightPlane =
		normalize(cross(bottomRight, topRight));
	const float3 topPlane =
		normalize(cross(topRight, topLeft));
	const float3 bottomPlane =
		normalize(cross(bottomLeft, bottomRight));

	if (groupThreadIndex <
		FORWARD_PLUS_TILE_LIGHT_CAPACITY &&
		groupThreadIndex < g_Pass.LightCount)
	{
		const uint lightIndex =
			g_Pass.LightBaseIndex +
			groupThreadIndex;
		const LightData light =
			g_ForwardPlusLights[lightIndex];
		const bool isLocalLight =
			light.LightType == 1u ||
			light.LightType == 2u;
		if (isLocalLight &&
			light.Intensity > 0.0 &&
			light.Range > 0.0)
		{
			const float3 centerVS = mul(
				float4(light.Position.xyz, 1.0),
				viewData.ViewMat).xyz;
			g_LightHits[groupThreadIndex] =
				IntersectsTileFrustum(
					centerVS,
					light.Range,
					minViewZ,
					maxViewZ,
					leftPlane,
					rightPlane,
					topPlane,
					bottomPlane) ?
					1u :
					0u;
		}
	}
	GroupMemoryBarrierWithGroupSync();

	for (uint scanOffset = 1u;
		scanOffset <
			FORWARD_PLUS_TILE_LIGHT_CAPACITY;
		scanOffset <<= 1u)
	{
		uint prefixValue = 0u;
		if (groupThreadIndex <
				FORWARD_PLUS_TILE_LIGHT_CAPACITY &&
			groupThreadIndex >= scanOffset)
		{
			prefixValue =
				g_LightHits[
					groupThreadIndex -
						scanOffset];
		}
		GroupMemoryBarrierWithGroupSync();
		if (groupThreadIndex <
			FORWARD_PLUS_TILE_LIGHT_CAPACITY)
		{
			g_LightHits[groupThreadIndex] +=
				prefixValue;
		}
		GroupMemoryBarrierWithGroupSync();
	}

	if (groupThreadIndex == 0)
	{
		g_TileLightHeaders[tileIndex] =
			uint2(
				tileOffset,
				g_LightHits[
					FORWARD_PLUS_TILE_LIGHT_CAPACITY -
						1u]);
	}
	if (groupThreadIndex <
			FORWARD_PLUS_TILE_LIGHT_CAPACITY &&
		groupThreadIndex < g_Pass.LightCount)
	{
		const uint inclusivePrefix =
			g_LightHits[groupThreadIndex];
		const uint previousPrefix =
			groupThreadIndex > 0u ?
				g_LightHits[
					groupThreadIndex - 1u] :
				0u;
		if (inclusivePrefix > previousPrefix)
		{
			g_TileLightIndices[
				tileOffset +
				previousPrefix] =
					g_Pass.LightBaseIndex +
					groupThreadIndex;
		}
	}
}
