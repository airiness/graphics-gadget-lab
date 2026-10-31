#include <Common/Common.hlsli>
#include <Common/ApplicationBinding.hlsli>
#include <Common/DepthReconstruction.hlsli>
#include <Common/VertexTransform.hlsli>
#include <Lighting/GIHitLighting.hlsli>
#include <Lighting/GIShaderConstants.hlsli>

StructuredBuffer<float4> ProbeRayInput : register(t11);
RWStructuredBuffer<float4> ProbeRayOutput : register(u1);

// Field order matches GIProbeUpdate.cpp::Parameters, including explicit padding.
cbuffer Parameters : register(b2)
{
	uint3 GridSize;
	uint RaysPerProbe;
	uint FirstProbe;
	uint UpdateProbeCount;
	uint ProbeCount;
	uint UpdateIndex;
	float3 GridMinimum;
	float ProbeSpacing;
	float3 ConstantRadiance;
	float MaximumDistance;
	uint3 SceneCounts;
	uint ConstantEnvironment;
	uint IrradianceIndex;
	uint DistanceIndex;
	uint StateIndex;
	uint TargetsPadding;
	uint TransmittanceIndex;
	uint AtmosphereSampler;
	uint LightingPadding;
	uint TraverseScene;
	float4 Rotation;
	float Hysteresis;
	uint RotateDirections;
	uint TraversalOnly;
	uint Padding;
};

static const float ProbeGoldenAngleTurns = 0.3819660112501051;
static const float ProbeBackfaceDistanceScale = -0.2;
static const float ProbeDistanceWeightExponent = 50.0;
// Distance moments stop at 1.5 cell diagonals, independent of the tracing interval.
static const float ProbeDistanceCellDiagonals = 1.5;
static const float ProbeMinimumIntegrationWeight = 1.0e-20;

RWTexture2DArray<float4> IrradianceAtlas()
{
	return ResourceDescriptorHeap[NonUniformResourceIndex(IrradianceIndex)];
}
RWTexture2DArray<float2> DistanceAtlas()
{
	return ResourceDescriptorHeap[NonUniformResourceIndex(DistanceIndex)];
}
RWTexture2DArray<float4> ProbeState()
{
	return ResourceDescriptorHeap[NonUniformResourceIndex(StateIndex)];
}
uint3 ProbeCoordinate(uint probe)
{
	// Atlas XY is world XZ; array layers are world Y.
	return uint3(probe % GridSize.x, (probe / GridSize.x) % GridSize.z,
		probe / (GridSize.x * GridSize.z));
}
float3 ProbeDirection(uint ray)
{
	const float z = 1.0 - 2.0 * ((float)(ray) + 0.5) / (float)(RaysPerProbe);
	// Bound sin/cos arguments: large-angle error is amplified at grazing texels.
	// Trace and both integration kernels must use the same phase.
	precise float turns = (float)(ray) * ProbeGoldenAngleTurns;
	precise float phase = frac(turns + 0.5) - 0.5;
	precise float angle = phase * TWO_PI;
	const float radius = sqrt(max(0.0, 1.0 - z * z));
	float3 direction = float3(radius * cos(angle), radius * sin(angle), z);
	if (RotateDirections != 0)
	{
		direction += 2.0 * cross(Rotation.xyz, cross(Rotation.xyz, direction) + Rotation.w * direction);
	}
	return direction;
}
float3 OctahedralDirection(uint2 texel, uint size)
{
	const float2 uv = 2.0 * ((float2)(texel) + 0.5) / (float)(size) - 1.0;
	float3 direction = float3(uv, 1.0 - abs(uv.x) - abs(uv.y));
	if (direction.z < 0.0)
	{
		direction.xy = (1.0 - abs(direction.yx)) * float2(direction.x >= 0 ? 1 : -1,
			direction.y >= 0 ? 1 : -1);
	}
	return normalize(direction);
}

[numthreads(GIQueryGroupWidth, GIQueryGroupHeight, 1)]
void Initialize(uint3 id : SV_DispatchThreadID)
{
	if (id.x < GridSize.x * GIProbeIrradianceTileWidth &&
		id.y < GridSize.z * GIProbeIrradianceTileWidth && id.z < GridSize.y)
	{
		IrradianceAtlas()[id] = 0.0.xxxx;
	}
	if (id.x < GridSize.x * GIProbeDistanceTileWidth &&
		id.y < GridSize.z * GIProbeDistanceTileWidth && id.z < GridSize.y)
	{
		DistanceAtlas()[id] = 0.0.xx;
	}
	if (id.x < GridSize.x && id.y < GridSize.z && id.z < GridSize.y)
	{
		ProbeState()[id] = 0.0.xxxx;
	}
	// All ray slots are defined once, including probes outside a partial update.
	const uint linearIndex = id.x + GridSize.x * GIProbeDistanceTileWidth *
		(id.y + GridSize.z * GIProbeDistanceTileWidth * id.z);
	const uint capacity = ProbeCount * RaysPerProbe;
	const uint threads = ProbeCount * GIProbeDistanceTileWidth * GIProbeDistanceTileWidth;
	for (uint index = linearIndex; index < capacity; index += threads)
	{
		ProbeRayOutput[index] = 0.0.xxxx;
	}
}

groupshared uint4 QueryCounts[GIProbeGroupThreads];
[numthreads(GIProbeGroupThreads, 1, 1)]
void Trace(uint3 group : SV_GroupID, uint thread : SV_GroupIndex)
{
	const uint probe = FirstProbe + group.x;
	const uint3 coordinate = ProbeCoordinate(probe);
	const float3 origin = GridMinimum + ProbeSpacing * (float3)(coordinate.xzy);
	uint4 counts = 0; // Miss, surface, backface, invalid.
	const float mip = ConstantEnvironment == 0 ? GIEnvironmentMip(0) : 0.0;
	for (uint index = thread; index < RaysPerProbe; index += GIProbeGroupThreads)
	{
		const float3 direction = ProbeDirection(index);
		float3 radiance = ConstantRadiance;
		float distance = MaximumDistance;
		uint outcome = GIHitMiss;
		if (TraverseScene != 0)
		{
			RayDesc ray;
			ray.Origin = origin;
			ray.Direction = direction;
			ray.TMin = 0.0;
			ray.TMax = MaximumDistance;
			if (TraversalOnly != 0)
			{
				RayQuery<RAY_FLAG_FORCE_OPAQUE> query;
				query.TraceRayInline(GIRayScene, RAY_FLAG_FORCE_OPAQUE, RaySceneOpaqueMask, ray);
				while (query.Proceed())
				{
				}
				if (query.CommittedStatus() == COMMITTED_TRIANGLE_HIT)
				{
					distance = query.CommittedRayT();
					outcome = GIHitSurface;
				}
				radiance = 0.0.xxx;
			}
			else
			{
				GIHit hit;
				outcome = GITraceHit(ray, SceneCounts, hit, false);
				if (outcome == GIHitSurface)
				{
					distance = length(hit.Position - origin);
					radiance = EvaluateGIHitSun(hit, MaximumDistance, TransmittanceIndex, AtmosphereSampler);
				}
				else if (outcome == GIHitBackface)
				{
					distance = ProbeBackfaceDistanceScale * length(hit.Position - origin);
					radiance = 0.0.xxx;
				}
				else if (outcome == GIHitMiss)
				{
					radiance = EvaluateGIMiss(direction, ConstantEnvironment != 0, ConstantRadiance, mip);
				}
			}
		}
		if (outcome == GIHitInvalid || !all(isfinite(NonFiniteObserveValue(
			NF_SITE_GI_PROBE_UPDATE_TRACE_ISFINITE, float4(radiance, distance)))))
		{
			// Invalid samples remain visible in state and poison the numerical export.
			radiance = asfloat(0x7fc00000u).xxx;
			distance = asfloat(0x7fc00000u);
			counts.w++;
		}
		else if (outcome == GIHitMiss)
		{
			counts.x++;
		}
		else if (outcome == GIHitSurface)
		{
			counts.y++;
		}
		else
		{
			counts.z++;
		}
		ProbeRayOutput[probe * RaysPerProbe + index] = float4(radiance, distance);
	}
	QueryCounts[thread] = counts;
	GroupMemoryBarrierWithGroupSync();
	for (uint stride = GIProbeGroupThreads / 2; stride != 0; stride >>= 1)
	{
		if (thread < stride)
		{
			QueryCounts[thread] += QueryCounts[thread + stride];
		}
		GroupMemoryBarrierWithGroupSync();
	}
	if (thread == 0)
	{
		ProbeState()[coordinate] = (float4)(QueryCounts[0]) / (float)(RaysPerProbe);
	}
}

groupshared float4 SharedRays[GIProbeMaximumRays];
groupshared float3 SharedDirections[GIProbeMaximumRays];
float4 IntegrateTexel(uint probe, uint texel, bool useShared)
{
	const bool irradiance = texel < GIProbeIrradianceInteriorTexels;
	const uint side = irradiance ? GIProbeIrradianceInteriorWidth : GIProbeDistanceInteriorWidth;
	const uint linearIndex = irradiance ? texel : texel - GIProbeIrradianceInteriorTexels;
	const float3 direction = OctahedralDirection(uint2(linearIndex % side, linearIndex / side), side);
	float3 radiance = 0.0.xxx;
	float2 moments = 0.0.xx;
	float sum = 0.0;
	for (uint ray = 0; ray < RaysPerProbe; ++ray)
	{
		const float4 value = useShared ? SharedRays[ray] : ProbeRayInput[probe * RaysPerProbe + ray];
		const float cosine = max(dot(direction, SharedDirections[ray]), 0.0);
		const float weight = irradiance ? cosine : pow(cosine, ProbeDistanceWeightExponent);
		const float distance = min(abs(value.w), ProbeDistanceCellDiagonals * sqrt(3.0) * ProbeSpacing);
		radiance += weight * value.xyz;
		moments += weight * float2(distance, distance * distance);
		sum += weight;
	}
	return irradiance ?
		float4(radiance / max(sum, ProbeMinimumIntegrationWeight), 1) :
		float4(moments / max(sum, ProbeMinimumIntegrationWeight), 0, 0);
}
void StoreTexel(uint probe, uint texel, float4 value)
{
	const uint3 coordinate = ProbeCoordinate(probe);
	if (texel < GIProbeIrradianceInteriorTexels)
	{
		const uint3 target = uint3(coordinate.xy * GIProbeIrradianceTileWidth + GIProbeBorderWidth +
			uint2(texel % GIProbeIrradianceInteriorWidth, texel / GIProbeIrradianceInteriorWidth), coordinate.z);
		const float4 previous = IrradianceAtlas()[target];
		IrradianceAtlas()[target] = float4(lerp(value.xyz, previous.xyz, Hysteresis), 1);
	}
	else
	{
		const uint index = texel - GIProbeIrradianceInteriorTexels;
		const uint3 target = uint3(coordinate.xy * GIProbeDistanceTileWidth + GIProbeBorderWidth +
			uint2(index % GIProbeDistanceInteriorWidth, index / GIProbeDistanceInteriorWidth), coordinate.z);
		const float2 previous = DistanceAtlas()[target];
		DistanceAtlas()[target] = lerp(value.xy, previous, Hysteresis);
	}
}
[numthreads(GIProbeGroupThreads, 1, 1)]
void BlendDirect(uint3 group : SV_GroupID, uint thread : SV_GroupIndex)
{
	const uint probe = FirstProbe + group.x;
	for (uint ray = thread; ray < RaysPerProbe; ray += GIProbeGroupThreads)
	{
		SharedDirections[ray] = ProbeDirection(ray);
	}
	GroupMemoryBarrierWithGroupSync();
	for (uint texel = thread; texel < GIProbeInteriorTexels; texel += GIProbeGroupThreads)
	{
		StoreTexel(probe, texel, IntegrateTexel(probe, texel, false));
	}
}
[numthreads(GIProbeGroupThreads, 1, 1)]
void BlendShared(uint3 group : SV_GroupID, uint thread : SV_GroupIndex)
{
	const uint probe = FirstProbe + group.x;
	for (uint ray = thread; ray < RaysPerProbe; ray += GIProbeGroupThreads)
	{
		SharedRays[ray] = ProbeRayInput[probe * RaysPerProbe + ray];
		SharedDirections[ray] = ProbeDirection(ray);
	}
	GroupMemoryBarrierWithGroupSync();
	for (uint texel = thread; texel < GIProbeInteriorTexels; texel += GIProbeGroupThreads)
	{
		StoreTexel(probe, texel, IntegrateTexel(probe, texel, true));
	}
}
uint2 BorderSource(uint2 pixel, uint side)
{
	const uint last = side - GIProbeBorderWidth;
	if (pixel.x == 0 && pixel.y == 0)
	{
		return last.xx;
	}
	if (pixel.x == side && pixel.y == 0)
	{
		return uint2(GIProbeBorderWidth, last);
	}
	if (pixel.x == 0 && pixel.y == side)
	{
		return uint2(last, GIProbeBorderWidth);
	}
	if (pixel.x == side && pixel.y == side)
	{
		return GIProbeBorderWidth.xx;
	}
	if (pixel.x == 0 || pixel.x == side)
	{
		return uint2(pixel.x == 0 ? GIProbeBorderWidth : last, side - pixel.y);
	}
	return uint2(side - pixel.x, pixel.y == 0 ? GIProbeBorderWidth : last);
}
[numthreads(GIProbeGroupThreads, 1, 1)]
void Borders(uint3 group : SV_GroupID, uint thread : SV_GroupIndex)
{
	const uint probe = FirstProbe + group.x;
	const uint3 coordinate = ProbeCoordinate(probe);
	for (uint index = thread; index < GIProbeTileTexels; index += GIProbeGroupThreads)
	{
		const bool irradiance = index < GIProbeIrradianceTileTexels;
		const uint width = irradiance ? GIProbeIrradianceTileWidth : GIProbeDistanceTileWidth;
		const uint linearIndex = irradiance ? index : index - GIProbeIrradianceTileTexels;
		const uint2 local = uint2(linearIndex % width, linearIndex / width);
		if (local.x != 0 && local.y != 0 && local.x != width - 1 && local.y != width - 1)
		{
			continue;
		}
		const uint2 source = BorderSource(local, width - 1);
		const uint3 target = uint3(coordinate.xy * width + local, coordinate.z);
		const uint3 origin = uint3(coordinate.xy * width + source, coordinate.z);
		if (irradiance)
		{
			IrradianceAtlas()[target] = IrradianceAtlas()[origin];
		}
		else
		{
			DistanceAtlas()[target] = DistanceAtlas()[origin];
		}
	}
}
