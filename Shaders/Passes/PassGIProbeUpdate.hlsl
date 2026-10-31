#include <Common/Common.hlsli>
#include <Common/ApplicationBinding.hlsli>
#include <Common/DepthReconstruction.hlsli>
#include <Common/VertexTransform.hlsli>
#include <Lighting/GIHitLighting.hlsli>

StructuredBuffer<float4> ProbeRayInput : register(t11);
RWStructuredBuffer<float4> ProbeRayOutput : register(u1);

cbuffer Parameters : register(b2)
{
	uint4 GridRays;
	uint4 RangeUpdate;
	float4 MinimumSpacing;
	float4 RadianceDistance;
	uint4 SceneMode;
	uint4 Targets;
	uint4 Lighting;
	float4 Rotation;
	float Hysteresis;
	uint RotateDirections;
	uint TraversalOnly;
	uint Padding;
};

RWTexture2DArray<float4> IrradianceAtlas()
{
	return ResourceDescriptorHeap[NonUniformResourceIndex(Targets.x)];
}
RWTexture2DArray<float2> DistanceAtlas()
{
	return ResourceDescriptorHeap[NonUniformResourceIndex(Targets.y)];
}
RWTexture2DArray<float4> ProbeState()
{
	return ResourceDescriptorHeap[NonUniformResourceIndex(Targets.z)];
}
uint3 ProbeCoordinate(uint probe)
{
	return uint3(probe % GridRays.x, (probe / GridRays.x) % GridRays.z,
		probe / (GridRays.x * GridRays.z));
}
float3 ProbeDirection(uint ray)
{
	const float z = 1.0 - 2.0 * ((float)(ray) + 0.5) / (float)(GridRays.w);
	// Keep the intrinsic argument bounded: large-angle sin/cos error is amplified
	// by bright samples at grazing integration texels. Trace and blend use this phase.
	precise float turns = (float)(ray) * 0.3819660112501051;
	precise float phase = frac(turns + 0.5) - 0.5;
	precise float angle = phase * 6.283185307179586;
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

[numthreads(8, 8, 1)]
void Initialize(uint3 id : SV_DispatchThreadID)
{
	if (id.x < GridRays.x * 8 && id.y < GridRays.z * 8 && id.z < GridRays.y)
	{
		IrradianceAtlas()[id] = 0.0.xxxx;
	}
	if (id.x < GridRays.x * 16 && id.y < GridRays.z * 16 && id.z < GridRays.y)
	{
		DistanceAtlas()[id] = 0.0.xx;
	}
	if (id.x < GridRays.x && id.y < GridRays.z && id.z < GridRays.y)
	{
		ProbeState()[id] = 0.0.xxxx;
	}
	// All ray slots are defined once, including probes not selected by a partial update.
	const uint linearIndex = id.x + GridRays.x * 16 * (id.y + GridRays.z * 16 * id.z);
	const uint capacity = RangeUpdate.z * GridRays.w;
	const uint threads = GridRays.x * GridRays.z * GridRays.y * 256;
	for (uint index = linearIndex; index < capacity; index += threads)
	{
		ProbeRayOutput[index] = 0.0.xxxx;
	}
}

groupshared uint4 QueryCounts[64];
[numthreads(64, 1, 1)]
void Trace(uint3 group : SV_GroupID, uint thread : SV_GroupIndex)
{
	const uint probe = RangeUpdate.x + group.x;
	const uint3 coordinate = ProbeCoordinate(probe);
	const float3 origin = MinimumSpacing.xyz + MinimumSpacing.w * (float3)(coordinate.xzy);
	uint4 counts = 0;
	const float mip = SceneMode.w == 0 ? GIEnvironmentMip(0) : 0.0;
	for (uint index = thread; index < GridRays.w; index += 64)
	{
		const float3 direction = ProbeDirection(index);
		float3 radiance = RadianceDistance.xyz;
		float distance = RadianceDistance.w;
		uint outcome = GIHitMiss;
		if (Lighting.w != 0)
		{
			RayDesc ray;
			ray.Origin = origin;
			ray.Direction = direction;
			ray.TMin = 0.0;
			ray.TMax = RadianceDistance.w;
			if (TraversalOnly != 0)
			{
				RayQuery<RAY_FLAG_FORCE_OPAQUE> query;
				query.TraceRayInline(GIRayScene, RAY_FLAG_FORCE_OPAQUE, 1, ray);
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
				outcome = GITraceHit(ray, SceneMode.xyz, hit, false);
				if (outcome == GIHitSurface)
				{
					distance = length(hit.Position - origin);
					radiance = EvaluateGIHitSun(hit, RadianceDistance.w, Lighting.x, Lighting.y);
				}
				else if (outcome == GIHitBackface)
				{
					distance = -0.2 * length(hit.Position - origin);
					radiance = 0.0.xxx;
				}
				else if (outcome == GIHitMiss)
				{
					radiance = EvaluateGIMiss(direction, SceneMode.w != 0, RadianceDistance.xyz, mip);
				}
			}
		}
		if (outcome == GIHitInvalid || !all(isfinite(NonFiniteObserveValue(NF_SITE_GI_PROBE_UPDATE_TRACE_ISFINITE, float4(radiance, distance)))))
		{
			// An invalid sample is visible in state and poisons the numerical export.
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
		ProbeRayOutput[probe * GridRays.w + index] = float4(radiance, distance);
	}
	QueryCounts[thread] = counts;
	GroupMemoryBarrierWithGroupSync();
	for (uint stride = 32; stride != 0; stride >>= 1)
	{
		if (thread < stride)
		{
			QueryCounts[thread] += QueryCounts[thread + stride];
		}
		GroupMemoryBarrierWithGroupSync();
	}
	if (thread == 0)
	{
		ProbeState()[coordinate] = (float4)(QueryCounts[0]) / (float)(GridRays.w);
	}
}

groupshared float4 SharedRays[256];
groupshared float3 SharedDirections[256];
float4 IntegrateTexel(uint probe, uint texel, bool useShared)
{
	const bool irradiance = texel < 36;
	const uint side = irradiance ? 6 : 14;
	const uint linearIndex = irradiance ? texel : texel - 36;
	const float3 direction = OctahedralDirection(uint2(linearIndex % side, linearIndex / side), side);
	float3 radiance = 0.0.xxx;
	float2 moments = 0.0.xx;
	float sum = 0.0;
	for (uint ray = 0; ray < GridRays.w; ++ray)
	{
		const float4 value = useShared ? SharedRays[ray] : ProbeRayInput[probe * GridRays.w + ray];
		const float cosine = max(dot(direction, SharedDirections[ray]), 0.0);
		const float weight = irradiance ? cosine : pow(cosine, 50.0);
		const float distance = min(abs(value.w), 1.5 * sqrt(3.0) * MinimumSpacing.w);
		radiance += weight * value.xyz;
		moments += weight * float2(distance, distance * distance);
		sum += weight;
	}
	return irradiance ? float4(radiance / max(sum, 1.0e-20), 1) :
		float4(moments / max(sum, 1.0e-20), 0, 0);
}
void StoreTexel(uint probe, uint texel, float4 value)
{
	const uint3 coordinate = ProbeCoordinate(probe);
	if (texel < 36)
	{
		const uint3 target = uint3(coordinate.xy * 8 + 1 + uint2(texel % 6, texel / 6), coordinate.z);
		const float4 previous = IrradianceAtlas()[target];
		IrradianceAtlas()[target] = float4(lerp(value.xyz, previous.xyz, Hysteresis), 1);
	}
	else
	{
		const uint index = texel - 36;
		const uint3 target = uint3(coordinate.xy * 16 + 1 + uint2(index % 14, index / 14), coordinate.z);
		const float2 previous = DistanceAtlas()[target];
		DistanceAtlas()[target] = lerp(value.xy, previous, Hysteresis);
	}
}
[numthreads(64, 1, 1)]
void BlendDirect(uint3 group : SV_GroupID, uint thread : SV_GroupIndex)
{
	const uint probe = RangeUpdate.x + group.x;
	for (uint ray = thread; ray < GridRays.w; ray += 64)
	{
		SharedDirections[ray] = ProbeDirection(ray);
	}
	GroupMemoryBarrierWithGroupSync();
	for (uint texel = thread; texel < 232; texel += 64)
	{
		StoreTexel(probe, texel, IntegrateTexel(probe, texel, false));
	}
}
[numthreads(64, 1, 1)]
void BlendShared(uint3 group : SV_GroupID, uint thread : SV_GroupIndex)
{
	const uint probe = RangeUpdate.x + group.x;
	for (uint ray = thread; ray < GridRays.w; ray += 64)
	{
		SharedRays[ray] = ProbeRayInput[probe * GridRays.w + ray];
		SharedDirections[ray] = ProbeDirection(ray);
	}
	GroupMemoryBarrierWithGroupSync();
	for (uint texel = thread; texel < 232; texel += 64)
	{
		StoreTexel(probe, texel, IntegrateTexel(probe, texel, true));
	}
}
uint2 BorderSource(uint2 pixel, uint side)
{
	const uint last = side - 1;
	if (pixel.x == 0 && pixel.y == 0)
	{
		return last.xx;
	}
	if (pixel.x == side && pixel.y == 0)
	{
		return uint2(1, last);
	}
	if (pixel.x == 0 && pixel.y == side)
	{
		return uint2(last, 1);
	}
	if (pixel.x == side && pixel.y == side)
	{
		return 1u.xx;
	}
	if (pixel.x == 0 || pixel.x == side)
	{
		return uint2(pixel.x == 0 ? 1 : last, side - pixel.y);
	}
	return uint2(side - pixel.x, pixel.y == 0 ? 1 : last);
}
[numthreads(64, 1, 1)]
void Borders(uint3 group : SV_GroupID, uint thread : SV_GroupIndex)
{
	const uint probe = RangeUpdate.x + group.x;
	const uint3 coordinate = ProbeCoordinate(probe);
	for (uint index = thread; index < 320; index += 64)
	{
		const bool irradiance = index < 64;
		const uint width = irradiance ? 8 : 16;
		const uint linearIndex = irradiance ? index : index - 64;
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
