#include <Common/Common.hlsli>
#include <Common/ApplicationBinding.hlsli>
#include <Common/DepthReconstruction.hlsli>
#include <Common/VertexTransform.hlsli>
#include <Lighting/GIHitLighting.hlsli>
#include <Lighting/GIShaderConstants.hlsli>
#include <Lighting/GISampling.hlsli>
#include <Lighting/GIFieldSampling.hlsli>

StructuredBuffer<float4> ProbeRayInput : register(t11);
RWStructuredBuffer<float4> ProbeRayOutput : register(u1);
RWStructuredBuffer<uint4> ProbeUpdateStatus : register(u2);
RWStructuredBuffer<float4> ProbeReadiness : register(u3);
RWStructuredBuffer<uint4> ProbeFeedback : register(u4);

// Field order matches GIProbeUpdateWorkload.cpp::Parameters, including explicit padding.
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
	uint ClassificationIndex;
	uint TransmittanceIndex;
	uint AtmosphereSampler;
	uint FieldFlags; // 1: full-volume field, 2: previous-field feedback, 4: progressive sampling.
	uint TraverseScene;
	float4 Rotation;
	float Hysteresis;
	uint RotateDirections;
	uint TraversalOnly;
	uint IrradianceInteriorWidth;
	uint ReadinessEnabled;
	uint ReadinessEpoch;
	float DistanceHysteresis;
	uint ReadinessDistance;
	float FeedbackViewBias;
	uint InspectionIndex;
	uint InspectedProbe;
	uint InspectionReplayIndex;
	float4 InspectionReplayRotation;
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
uint IrradianceTileWidth()
{
	return IrradianceInteriorWidth + 2 * GIProbeBorderWidth;
}
uint IrradianceInteriorTexels()
{
	return IrradianceInteriorWidth * IrradianceInteriorWidth;
}
uint3 ProbeCoordinate(uint probe)
{
	// Atlas XY is world XZ; array layers are world Y.
	return uint3(probe % GridSize.x, (probe / GridSize.x) % GridSize.z,
		probe / (GridSize.x * GridSize.z));
}
float3 ProbeBaseDirection(uint ray, uint directionUpdate)
{
	if ((FieldFlags & 4u) != 0)
	{
		// The 32-bit sample index wraps after 2^32 rays, not at a lighting epoch.
		const uint index = (RotateDirections != 0 ? directionUpdate : 0) * RaysPerProbe + ray;
		const float u = GIUniformOpenInterval(GIProgressiveSampleBits(index, 0));
		const float v = GIUniformOpenInterval(GIProgressiveSampleBits(index, 1));
		const float z = 1.0 - 2.0 * u;
		precise float angle = (v - 0.5) * TWO_PI;
		const float radius = sqrt(max(0.0, 1.0 - z * z));
		return float3(radius * cos(angle), radius * sin(angle), z);
	}
	const float z = 1.0 - 2.0 * ((float)(ray) + 0.5) / (float)(RaysPerProbe);
	// Bound sin/cos arguments: large-angle error is amplified at grazing texels.
	// Trace and both integration kernels must use the same phase.
	precise float turns = (float)(ray) * ProbeGoldenAngleTurns;
	precise float phase = frac(turns + 0.5) - 0.5;
	precise float angle = phase * TWO_PI;
	const float radius = sqrt(max(0.0, 1.0 - z * z));
	return float3(radius * cos(angle), radius * sin(angle), z);
}
float3 ProbeDirection(uint probe, uint ray, uint directionUpdate, float4 commonRotation)
{
	float3 direction = ProbeBaseDirection(ray, directionUpdate);
	if ((FieldFlags & 1u) != 0)
	{
		// A common rotation correlates every cage corner's narrow-emitter error.
		// Decorrelate both probes and updates: nearby CPU seeds otherwise permute
		// overlapping histories. The ordinary probe experiment keeps common directions.
		// Progressive blocks keep both rotations fixed across submitted updates.
		const uint update = RotateDirections != 0 && (FieldFlags & 4u) == 0 ?
			GIHash(directionUpdate) :
			0;
		uint state = GIHash(probe ^ update);
		const float u = GIRandom(state);
		const float v = TWO_PI * GIRandom(state);
		const float w = TWO_PI * GIRandom(state);
		const float4 rotation = float4(sqrt(1.0 - u) * sin(v), sqrt(1.0 - u) * cos(v),
			sqrt(u) * sin(w), sqrt(u) * cos(w));
		direction += 2.0 * cross(rotation.xyz, cross(rotation.xyz, direction) + rotation.w * direction);
	}
	if (RotateDirections != 0)
	{
		direction += 2.0 * cross(commonRotation.xyz,
			cross(commonRotation.xyz, direction) + commonRotation.w * direction);
	}
	return direction;
}
float3 ProbeDirection(uint probe, uint ray)
{
	return ProbeDirection(probe, ray, UpdateIndex, Rotation);
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

// Versioned, opt-in witness. It observes inputs/intermediates, never feeds the estimator.
void WriteIntegrationRecord(uint column, uint row, uint4 value)
{
	RWTexture2D<uint> output = ResourceDescriptorHeap[NonUniformResourceIndex(InspectionIndex)];
	const uint2 pixel = uint2(4 * column, row);
	output[pixel] = value.x;
	output[pixel + uint2(1, 0)] = value.y;
	output[pixel + uint2(2, 0)] = value.z;
	output[pixel + uint2(3, 0)] = value.w;
}
float4 ReadIntegrationRecord(uint column, uint row)
{
	RWTexture2D<uint> input = ResourceDescriptorHeap[NonUniformResourceIndex(InspectionIndex)];
	const uint2 pixel = uint2(4 * column, row);
	return asfloat(uint4(input[pixel], input[pixel + uint2(1, 0)],
		input[pixel + uint2(2, 0)], input[pixel + uint2(3, 0)]));
}
uint IntegrationBlockRows()
{
	return (IrradianceInteriorTexels() + RaysPerProbe - 1) / RaysPerProbe;
}
void WriteIntegrationTexel(uint block, uint texel, float4 value)
{
	WriteIntegrationRecord(texel % RaysPerProbe,
		GIIntegrationInspectionHeaderRows + block * IntegrationBlockRows() + texel / RaysPerProbe,
		asuint(value));
}
[numthreads(GIProbeGroupThreads, 1, 1)]
void InspectIntegration(uint3 dispatch : SV_DispatchThreadID)
{
	const uint rows = GIIntegrationInspectionHeaderRows +
		GIIntegrationInspectionBlocks * IntegrationBlockRows() + IrradianceInteriorTexels();
	if (dispatch.x >= RaysPerProbe * rows)
	{
		return;
	}
	const uint column = dispatch.x % RaysPerProbe;
	const uint row = dispatch.x / RaysPerProbe;
	uint4 record = 0;
	if (row == 0)
	{
		if (column == 0)
		{
			record = uint4(GIIntegrationInspectionMagic, GIIntegrationInspectionVersion, UpdateIndex + 1, InspectedProbe);
		}
		else if (column == 1)
		{
			record = asuint(Rotation);
		}
		else if (column == 2)
		{
			record = asuint(InspectionReplayRotation);
		}
		else if (column == 3)
		{
			record = uint4(RaysPerProbe, IrradianceInteriorWidth, InspectionReplayIndex + 1, ReadinessEpoch);
		}
		else if (column == 4)
		{
			record = asuint(float4(Hysteresis, DistanceHysteresis, 0, 0));
		}
		else if (column == 5)
		{
			record = uint4(GridSize, RotateDirections);
		}
		else if (column == 6)
		{
			record = uint4(FieldFlags, 0, 0, 0);
		}
	}
	else if (row == 3)
	{
		record = asuint(float4(ProbeDirection(InspectedProbe, column,
			InspectionReplayIndex, InspectionReplayRotation), 0));
	}
	WriteIntegrationRecord(column, row, record);
}

void InvalidateUpdate(uint error, uint probe, uint element, bool publishField)
{
	if (publishField && ClassificationIndex != 0xffffffffu)
	{
		RWTexture2DArray<uint> classification = ResourceDescriptorHeap[NonUniformResourceIndex(ClassificationIndex)];
		InterlockedOr(classification[uint3(0, 0, 0)], GIProbeFieldInvalidBit);
	}
	uint previous;
	InterlockedCompareExchange(ProbeUpdateStatus[0].x, GIUpdateErrorNone, error, previous);
	if (previous == GIUpdateErrorNone)
	{
		ProbeUpdateStatus[0].yzw = uint3(probe, UpdateIndex, element);
	}
}

[numthreads(GIQueryGroupWidth, GIQueryGroupHeight, 1)]
void Initialize(uint3 id : SV_DispatchThreadID)
{
	if (all(id == 0))
	{
		ProbeUpdateStatus[0] = uint4(GIUpdateErrorNone, 0xffffffffu, 0xffffffffu, 0xffffffffu);
	}
	if (id.x < GridSize.x * IrradianceTileWidth() &&
		id.y < GridSize.z * IrradianceTileWidth() && id.z < GridSize.y)
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
		if ((FieldFlags & 2u) != 0)
		{
			const uint probe = id.x + GridSize.x * (id.y + GridSize.z * id.z);
			ProbeFeedback[2 * probe] = 0;
			ProbeFeedback[2 * probe + 1] = 0;
		}
		if (ClassificationIndex != 0xffffffffu)
		{
			RWTexture2DArray<uint> classification = ResourceDescriptorHeap[NonUniformResourceIndex(ClassificationIndex)];
			classification[id] = GIProbeUnclassified;
		}
	}
	if (ReadinessEnabled != 0)
	{
		const uint linearIndex = id.x + GridSize.x * max(IrradianceTileWidth(), GIProbeDistanceTileWidth) *
			(id.y + GridSize.z * max(IrradianceTileWidth(), GIProbeDistanceTileWidth) * id.z);
		const uint threads = ProbeCount * max(IrradianceTileWidth(), GIProbeDistanceTileWidth) *
			max(IrradianceTileWidth(), GIProbeDistanceTileWidth);
		for (uint element = linearIndex; element < 1 + 2 * ProbeCount; element += threads)
		{
			ProbeReadiness[element] = 0.0.xxxx;
		}
	}
	// All ray slots are defined once, including probes outside a partial update.
	const uint width = max(IrradianceTileWidth(), GIProbeDistanceTileWidth);
	const uint linearIndex = id.x + GridSize.x * width * (id.y + GridSize.z * width * id.z);
	const uint capacity = ProbeCount * RaysPerProbe;
	const uint threads = ProbeCount * width * width;
	for (uint index = linearIndex; index < capacity; index += threads)
	{
		ProbeRayOutput[index] = 0.0.xxxx;
	}
}

groupshared uint4 QueryCounts[GIProbeGroupThreads];
groupshared uint4 FeedbackSupport[GIProbeGroupThreads];
groupshared uint4 FeedbackObservation[GIProbeGroupThreads];
groupshared uint UpdateBlocked;
bool IsUpdateBlocked(uint thread, bool publishField = false)
{
	if (thread == 0)
	{
		UpdateBlocked = ProbeUpdateStatus[0].x;
	}
	// Every thread must take the same return path before subsequent group barriers.
	GroupMemoryBarrierWithGroupSync();
	if (publishField && thread == 0 && UpdateBlocked != GIUpdateErrorNone && ClassificationIndex != 0xffffffffu)
	{
		// Trace samples classification through an SRV. Publish its sticky verdict
		// here, after that read-only phase and before any field consumer.
		RWTexture2DArray<uint> classification = ResourceDescriptorHeap[NonUniformResourceIndex(ClassificationIndex)];
		InterlockedOr(classification[uint3(0, 0, 0)], GIProbeFieldInvalidBit);
	}
	if (publishField)
	{
		// Reconverge the thread-zero publisher before a caller starts another
		// group-shared phase; every lane must reach the cached-ray preload together.
		GroupMemoryBarrierWithGroupSync();
	}
	return UpdateBlocked != GIUpdateErrorNone;
}

// Classification uses a fixed, unrotated set, independent of update ray count
// and history. Only safe owner replacement can change this static geometry.
[numthreads(GIProbeGroupThreads, 1, 1)]
void Classify(uint3 group : SV_GroupID, uint thread : SV_GroupIndex)
{
	if (IsUpdateBlocked(thread))
	{
		return;
	}
	const uint probe = group.x;
	uint4 counts = 0;
	if (thread < GIProbeClassificationRays)
	{
		const float z = 1.0 - 2.0 * ((float)(thread) + 0.5) / (float)(GIProbeClassificationRays);
		const float angle = TWO_PI * (frac((float)(thread) * ProbeGoldenAngleTurns + 0.5) - 0.5);
		const float radius = sqrt(max(0.0, 1.0 - z * z));
		RayDesc ray;
		ray.Origin = GridMinimum + ProbeSpacing * (float3)(ProbeCoordinate(probe).xzy);
		ray.Direction = float3(radius * cos(angle), radius * sin(angle), z);
		ray.TMin = 0.0;
		ray.TMax = MaximumDistance;
		GIHit hit;
		const uint outcome = GITraceHit(ray, SceneCounts, hit, false);
		counts.x = outcome == GIHitBackface ? 1u : 0u;
		if (outcome == GIHitInvalid)
		{
			InvalidateUpdate(GIUpdateErrorInvalidRay, probe, thread, true);
			counts.y = 1;
		}
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
		RWTexture2DArray<uint> classification = ResourceDescriptorHeap[NonUniformResourceIndex(ClassificationIndex)];
		const uint value = QueryCounts[0].y != 0 ? GIProbeUnclassified :
			QueryCounts[0].x > GIProbeClassificationRays / 4 ? GIProbeInactive : GIProbeActive;
		// Preserve a concurrently reported failure at texel zero.
		InterlockedOr(classification[ProbeCoordinate(probe)], value);
	}
}

[numthreads(GIProbeGroupThreads, 1, 1)]
void Trace(uint3 group : SV_GroupID, uint thread : SV_GroupIndex)
{
	// The sticky status blocks later updates without destroying the first failure's rays.
	if (IsUpdateBlocked(thread))
	{
		return;
	}
	const uint probe = FirstProbe + group.x;
	const uint3 coordinate = ProbeCoordinate(probe);
	const float3 origin = GridMinimum + ProbeSpacing * (float3)(coordinate.xzy);
	uint4 counts = 0; // Miss, surface, backface, invalid.
	uint4 support = 0;
	uint4 observation = 0;
	const float mip = ConstantEnvironment == 0 ? GIEnvironmentMip(0) : 0.0;
	for (uint index = thread; index < RaysPerProbe; index += GIProbeGroupThreads)
	{
		const float3 direction = ProbeDirection(probe, index);
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
					if ((FieldFlags & 2u) != 0)
					{
						if (UpdateIndex == 0)
						{
							// No previous observation exists at bootstrap. Never query
							// freshly zeroed distance moments as if they were support.
							observation.x++;
						}
						else
						{
							GIFieldSamplingParameters field;
							field.Counts = GridSize;
							field.IrradianceInteriorWidth = IrradianceInteriorWidth;
							field.Minimum = GridMinimum;
							field.Spacing = ProbeSpacing;
							field.Irradiance = IrradianceIndex;
							field.Distance = DistanceIndex;
							field.Classification = ClassificationIndex;
							field.ViewBias = FeedbackViewBias;
							const GIFieldQuery query = SampleGIField(field, hit.Position,
								hit.VertexNormal, hit.VertexNormal, -direction);
							if (query.Compatible == 0)
							{
								observation.y++;
								outcome = GIHitInvalid;
							}
							else if (query.Support == GIFieldValid || query.Support == GIFieldLowConfidence)
							{
								// Shared raw field query: no IBL fallback, confidence
								// darkening, exposure or extra bounce-energy multiplier.
								radiance += hit.Albedo * query.Irradiance / PI;
								support[query.Support == GIFieldValid ? 0 : 1]++;
								observation.z = 1;
							}
							else if (query.Support == GIFieldNoValidSupport)
							{
								support.z++;
							}
							else
							{
								support.w++;
							}
						}
					}
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
		const float4 sampled = NonFiniteInject(NF_INJECT_PASS_GI_PROBE_UPDATE_TRACE_SAMPLE,
			uint2(probe, index), float4(radiance, distance));
		radiance = sampled.xyz;
		distance = sampled.w;
		if (outcome == GIHitInvalid || !all(isfinite(NonFiniteObserveValue(
			NF_SITE_GI_PROBE_UPDATE_TRACE_ISFINITE, float4(radiance, distance)))))
		{
			// Keep the raw diagnostic; no part of this update may become valid history.
			InvalidateUpdate(GIUpdateErrorInvalidRay, probe, index, false);
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
	FeedbackSupport[thread] = support;
	FeedbackObservation[thread] = observation;
	GroupMemoryBarrierWithGroupSync();
	for (uint stride = GIProbeGroupThreads / 2; stride != 0; stride >>= 1)
	{
		if (thread < stride)
		{
			QueryCounts[thread] += QueryCounts[thread + stride];
			FeedbackSupport[thread] += FeedbackSupport[thread + stride];
			FeedbackObservation[thread].xy += FeedbackObservation[thread + stride].xy;
			FeedbackObservation[thread].z |= FeedbackObservation[thread + stride].z;
		}
		GroupMemoryBarrierWithGroupSync();
	}
	if (thread == 0)
	{
		ProbeState()[coordinate] = (float4)(QueryCounts[0]) / (float)(RaysPerProbe);
		if ((FieldFlags & 2u) != 0)
		{
			ProbeFeedback[2 * probe] = FeedbackSupport[0];
			ProbeFeedback[2 * probe + 1] = uint4(FeedbackObservation[0].xyz, UpdateIndex + 1);
		}
	}
}

groupshared uint ReadinessCounts[GIProbeGroupThreads];
groupshared uint ReadinessActive;
groupshared uint ReadinessLow;
groupshared uint ReadinessHigh;
[numthreads(GIProbeGroupThreads, 1, 1)]
void ReadinessFloor(uint thread : SV_GroupIndex)
{
	if (IsUpdateBlocked(thread))
	{
		return;
	}
	RWTexture2DArray<uint> classification = ResourceDescriptorHeap[NonUniformResourceIndex(ClassificationIndex)];
	uint active = 0;
	for (uint probe = thread; probe < ProbeCount; probe += GIProbeGroupThreads)
	{
		active += classification[ProbeCoordinate(probe)] == GIProbeActive ? 1u : 0u;
	}
	ReadinessCounts[thread] = active;
	GroupMemoryBarrierWithGroupSync();
	for (uint stride = GIProbeGroupThreads / 2; stride != 0; stride >>= 1)
	{
		if (thread < stride)
		{
			ReadinessCounts[thread] += ReadinessCounts[thread + stride];
		}
		GroupMemoryBarrierWithGroupSync();
	}
	if (thread == 0)
	{
		ReadinessActive = ReadinessCounts[0];
		ReadinessLow = 0;
		ReadinessHigh = 0x7f7fffffu;
	}
	GroupMemoryBarrierWithGroupSync();
	if (ReadinessActive == 0)
	{
		if (thread == 0)
		{
			ProbeReadiness[0] = float4(GIFieldReadinessAbsoluteFloor, 0, 0, (float)(GIFieldReadinessChangeSpan));
		}
		return;
	}
	// Positive finite floats have monotonically ordered bit patterns.
	for (uint bit = 0; bit < 31; ++bit)
	{
		const uint middle = ReadinessLow + (ReadinessHigh - ReadinessLow) / 2;
		uint count = 0;
		for (uint probe = thread; probe < ProbeCount; probe += GIProbeGroupThreads)
		{
			if (classification[ProbeCoordinate(probe)] == GIProbeActive &&
				asuint(ProbeReadiness[2 + 2 * probe].x) <= middle)
			{
				++count;
			}
		}
		ReadinessCounts[thread] = count;
		GroupMemoryBarrierWithGroupSync();
		for (uint stride = GIProbeGroupThreads / 2; stride != 0; stride >>= 1)
		{
			if (thread < stride)
			{
				ReadinessCounts[thread] += ReadinessCounts[thread + stride];
			}
			GroupMemoryBarrierWithGroupSync();
		}
		if (thread == 0)
		{
			if (ReadinessCounts[0] > ReadinessActive / 2)
			{
				ReadinessHigh = middle;
			}
			else
			{
				ReadinessLow = middle + 1;
			}
		}
		GroupMemoryBarrierWithGroupSync();
	}
	if (thread == 0)
	{
		const float median = ReadinessActive == 0 ? 0.0 : asfloat(ReadinessLow);
		ProbeReadiness[0] = float4(max(GIFieldReadinessAbsoluteFloor, GIFieldReadinessRelativeFloor * median), median, (float)(ReadinessActive), (float)(GIFieldReadinessChangeSpan));
	}
}

groupshared float2 ReadinessMeasures[GIProbeGroupThreads];
groupshared float4 SharedRays[GIProbeMaximumRays];
groupshared float3 SharedDirections[GIProbeMaximumRays];
void InspectIntegrationInputs(uint probe, uint thread)
{
	if (InspectionIndex != 0xffffffffu && probe == InspectedProbe)
	{
		for (uint ray = thread; ray < RaysPerProbe; ray += GIProbeGroupThreads)
		{
			WriteIntegrationRecord(ray, 1, asuint(float4(SharedDirections[ray], 0)));
			WriteIntegrationRecord(ray, 2, asuint(ProbeRayInput[probe * RaysPerProbe + ray]));
		}
	}
}
float4 IntegrateTexel(uint probe, uint texel, bool useShared)
{
	const bool irradiance = texel < IrradianceInteriorTexels();
	const uint side = irradiance ? IrradianceInteriorWidth : GIProbeDistanceInteriorWidth;
	const uint linearIndex = irradiance ? texel : texel - IrradianceInteriorTexels();
	const float3 direction = OctahedralDirection(uint2(linearIndex % side, linearIndex / side), side);
	float3 radiance = 0.0.xxx;
	float2 moments = 0.0.xx;
	float sum = 0.0;
	for (uint ray = 0; ray < RaysPerProbe; ++ray)
	{
		const float4 value = useShared ? SharedRays[ray] : ProbeRayInput[probe * RaysPerProbe + ray];
		const float signedCosine = dot(direction, SharedDirections[ray]);
		const float cosine = max(signedCosine, 0.0);
		const float weight = irradiance ? cosine : pow(cosine, ProbeDistanceWeightExponent);
		const float distance = min(abs(value.w), ProbeDistanceCellDiagonals * sqrt(3.0) * ProbeSpacing);
		radiance += weight * value.xyz;
		moments += weight * float2(distance, distance * distance);
		sum += weight;
		if (InspectionIndex != 0xffffffffu && probe == InspectedProbe && irradiance)
		{
			const float3 replayDirection = ReadIntegrationRecord(ray, 3).xyz;
			const float replayCosine = dot(direction, replayDirection);
			WriteIntegrationRecord(ray, GIIntegrationInspectionHeaderRows +
				GIIntegrationInspectionBlocks * IntegrationBlockRows() + texel,
				asuint(float4(signedCosine, cosine, replayCosine, max(replayCosine, 0.0))));
		}
	}
	if (InspectionIndex != 0xffffffffu && probe == InspectedProbe && irradiance)
	{
		WriteIntegrationTexel(0, texel, float4(direction, 0));
		WriteIntegrationTexel(1, texel, float4(radiance / max(sum, ProbeMinimumIntegrationWeight), 1));
		WriteIntegrationTexel(4, texel, float4(radiance, sum));
	}
	return irradiance ?
		float4(radiance / max(sum, ProbeMinimumIntegrationWeight), 1) :
		float4(moments / max(sum, ProbeMinimumIntegrationWeight), 0, 0);
}
float2 StoreTexel(uint probe, uint texel, float4 value)
{
	const uint3 coordinate = ProbeCoordinate(probe);
	if (texel < IrradianceInteriorTexels())
	{
		const uint3 target = uint3(coordinate.xy * IrradianceTileWidth() + GIProbeBorderWidth +
			uint2(texel % IrradianceInteriorWidth, texel / IrradianceInteriorWidth), coordinate.z);
		const float4 previous = NonFiniteInject(NF_INJECT_PASS_GI_PROBE_UPDATE_STORETEXEL_HISTORY,
			uint2(probe, texel), IrradianceAtlas()[target]);
		if (!all(isfinite(NonFiniteObserveValue(NF_SITE_GI_PROBE_UPDATE_STORETEXEL_ISFINITE, previous))))
		{
			InvalidateUpdate(GIUpdateErrorInvalidHistory, probe, texel, true);
			return 0.0.xx;
		}
		const float3 blended = lerp(value.xyz, previous.xyz, Hysteresis);
		if (!all(isfinite(NonFiniteObserveValue(NF_SITE_GI_PROBE_UPDATE_STORETEXEL_ISFINITE, blended))))
		{
			InvalidateUpdate(GIUpdateErrorInvalidBlend, probe, texel, true);
			return 0.0.xx;
		}
		IrradianceAtlas()[target] = float4(blended, 1);
		if (InspectionIndex != 0xffffffffu && probe == InspectedProbe)
		{
			WriteIntegrationTexel(2, texel, previous);
			WriteIntegrationTexel(3, texel, float4(blended, 1));
		}
		if (ReadinessEnabled != 0)
		{
			const float3 change = abs(blended - previous.xyz) * PI /
				max(previous.xyz * PI, ProbeReadiness[0].x.xxx);
			return float2(max(change.x, max(change.y, change.z)),
				dot(blended * PI, float3(0.2126, 0.7152, 0.0722)));
		}
	}
	else
	{
		const uint index = texel - IrradianceInteriorTexels();
		const uint3 target = uint3(coordinate.xy * GIProbeDistanceTileWidth + GIProbeBorderWidth +
			uint2(index % GIProbeDistanceInteriorWidth, index / GIProbeDistanceInteriorWidth), coordinate.z);
		const float2 previous = NonFiniteInject(NF_INJECT_PASS_GI_PROBE_UPDATE_STORETEXEL_HISTORY,
			uint2(probe, texel), DistanceAtlas()[target]);
		if (!all(isfinite(NonFiniteObserveValue(NF_SITE_GI_PROBE_UPDATE_STORETEXEL_ISFINITE, previous))))
		{
			InvalidateUpdate(GIUpdateErrorInvalidHistory, probe, texel, true);
			return 0.0.xx;
		}
		const float2 blended = lerp(value.xy, previous, DistanceHysteresis);
		if (!all(isfinite(NonFiniteObserveValue(NF_SITE_GI_PROBE_UPDATE_STORETEXEL_ISFINITE, blended))))
		{
			InvalidateUpdate(GIUpdateErrorInvalidBlend, probe, texel, true);
			return 0.0.xx;
		}
		DistanceAtlas()[target] = blended;
		if (ReadinessEnabled != 0 && ReadinessDistance != 0)
		{
			return float2(abs(blended.x - previous.x) / ProbeSpacing, 0);
		}
	}
	return 0.0.xx;
}
[numthreads(GIProbeGroupThreads, 1, 1)]
void BlendDirect(uint3 group : SV_GroupID, uint thread : SV_GroupIndex)
{
	// Trace and blend publish a sticky GPU verdict before any later consumer.
	if (IsUpdateBlocked(thread, true))
	{
		return;
	}
	const uint probe = FirstProbe + group.x;
	for (uint ray = thread; ray < RaysPerProbe; ray += GIProbeGroupThreads)
	{
		SharedDirections[ray] = ProbeDirection(probe, ray);
	}
	GroupMemoryBarrierWithGroupSync();
	InspectIntegrationInputs(probe, thread);
	for (uint texel = thread; texel < IrradianceInteriorTexels() + GIProbeDistanceInteriorTexels;
		texel += GIProbeGroupThreads)
	{
		StoreTexel(probe, texel, IntegrateTexel(probe, texel, false));
	}
}
[numthreads(GIProbeGroupThreads, 1, 1)]
void BlendShared(uint3 group : SV_GroupID, uint thread : SV_GroupIndex)
{
	// Trace and blend publish a sticky GPU verdict before any later consumer.
	if (IsUpdateBlocked(thread, true))
	{
		return;
	}
	const uint probe = FirstProbe + group.x;
	for (uint ray = thread; ray < RaysPerProbe; ray += GIProbeGroupThreads)
	{
		SharedRays[ray] = ProbeRayInput[probe * RaysPerProbe + ray];
		SharedDirections[ray] = ProbeDirection(probe, ray);
	}
	GroupMemoryBarrierWithGroupSync();
	InspectIntegrationInputs(probe, thread);
	float2 measure = 0.0.xx;
	for (uint texel = thread; texel < IrradianceInteriorTexels() + GIProbeDistanceInteriorTexels;
		texel += GIProbeGroupThreads)
	{
		const float2 measurement = StoreTexel(probe, texel, IntegrateTexel(probe, texel, true));
		measure.x = max(measure.x, measurement.x);
		measure.y += measurement.y;
	}
	if (ReadinessEnabled != 0)
	{
		ReadinessMeasures[thread] = measure;
		GroupMemoryBarrierWithGroupSync();
		for (uint stride = GIProbeGroupThreads / 2; stride != 0; stride >>= 1)
		{
			if (thread < stride)
			{
				ReadinessMeasures[thread].x = max(ReadinessMeasures[thread].x, ReadinessMeasures[thread + stride].x);
				ReadinessMeasures[thread].y += ReadinessMeasures[thread + stride].y;
			}
			GroupMemoryBarrierWithGroupSync();
		}
		if (thread == 0)
		{
			const uint elapsed = UpdateIndex - ReadinessEpoch;
			float4 changes = elapsed == 0 ? 0.0.xxxx : ProbeReadiness[1 + 2 * probe];
			changes[elapsed % GIFieldReadinessChangeSpan] = ReadinessMeasures[0].x;
			ProbeReadiness[1 + 2 * probe] = changes;
			RWTexture2DArray<uint> classification = ResourceDescriptorHeap[NonUniformResourceIndex(ClassificationIndex)];
			ProbeReadiness[2 + 2 * probe] = float4(ReadinessMeasures[0].y / (float)(IrradianceInteriorTexels()),
				dot(changes, (1.0 / (float)(GIFieldReadinessChangeSpan)).xxxx), classification[ProbeCoordinate(probe)] == GIProbeActive ? 1.0 : 0.0,
				asfloat(elapsed + 1));
		}
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
	// Trace and blend publish a sticky GPU verdict before any later consumer.
	if (IsUpdateBlocked(thread))
	{
		return;
	}
	const uint probe = FirstProbe + group.x;
	const uint3 coordinate = ProbeCoordinate(probe);
	const uint irradianceTexels = IrradianceTileWidth() * IrradianceTileWidth();
	for (uint index = thread; index < irradianceTexels + GIProbeDistanceTileWidth * GIProbeDistanceTileWidth;
		index += GIProbeGroupThreads)
	{
		const bool irradiance = index < irradianceTexels;
		const uint width = irradiance ? IrradianceTileWidth() : GIProbeDistanceTileWidth;
		const uint linearIndex = irradiance ? index : index - irradianceTexels;
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
