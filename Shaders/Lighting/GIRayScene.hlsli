#pragma once
#include <Lighting/GIHitMaterial.hlsli>

RaytracingAccelerationStructure GIRayScene : register(t5);
ByteAddressBuffer GIVertices : register(t6);
ByteAddressBuffer GIIndices : register(t7);
StructuredBuffer<uint4> GIGeometries : register(t8);
StructuredBuffer<uint4> GIInstances : register(t9);
StructuredBuffer<MaterialData> GIMaterials : register(t10);

static const uint GIHitMiss = 0;
static const uint GIHitSurface = 1;
static const uint GIHitBackface = 2;
static const uint GIHitInvalid = 3;

struct GIHit
{
	float3 Position;
	float3 FaceNormal;
	float3 VertexNormal;
	float3 Albedo;
	float OriginError;
	uint Instance;
	uint Primitive;
};

// Normal-directed ULP offset (Ray Tracing Gems, chapter 6).
float3 GIOffsetOrigin(float3 position, float3 normal)
{
	const int3 offset = (int3)(256.0 * normal);
	const int3 bits = asint(position) + int3(position.x < 0.0 ? -offset.x : offset.x,
		position.y < 0.0 ? -offset.y : offset.y, position.z < 0.0 ? -offset.z : offset.z);
	const float3 shifted = asfloat(bits);
	return float3(abs(position.x) < 0.03125 ? position.x + normal.x / 65536.0 : shifted.x,
		abs(position.y) < 0.03125 ? position.y + normal.y / 65536.0 : shifted.y,
		abs(position.z) < 0.03125 ? position.z + normal.z / 65536.0 : shifted.z);
}

float3 GIHitOrigin(GIHit hit)
{
	return GIOffsetOrigin(hit.Position + hit.OriginError * hit.FaceNormal, hit.FaceNormal);
}

// Counts and bindings must describe the same immutable all-World snapshot.
// Secondary rays never cull backfaces: a single-sided backface is a zero-contribution
// HIT, not sky. A primary lookup instead matches raster sidedness, including at the
// coincident internal faces of touching closed solids.
uint GITraceHit(RayDesc ray, uint3 counts, out GIHit hit, bool rasterReceiver)
{
	hit = (GIHit)(0);
	RayQuery<RAY_FLAG_NONE> query;
	query.TraceRayInline(GIRayScene, rasterReceiver ? RAY_FLAG_FORCE_NON_OPAQUE : RAY_FLAG_FORCE_OPAQUE, 1, ray);
	while (query.Proceed())
	{
		if (query.CandidateType() != CANDIDATE_NON_OPAQUE_TRIANGLE ||
			query.CandidateInstanceID() >= counts.x)
		{
			return GIHitInvalid;
		}
		const uint4 candidate = GIInstances[query.CandidateInstanceID()];
		if (candidate.y >= counts.z || (candidate.z & 1u) == 0)
		{
			return GIHitInvalid;
		}
		if (query.CandidateTriangleFrontFace() || (GIMaterials[candidate.y].Flags & 1u) != 0)
		{
			query.CommitNonOpaqueTriangleHit();
		}
	}
	if (query.CommittedStatus() != COMMITTED_TRIANGLE_HIT)
	{
		return GIHitMiss;
	}
	hit.Instance = query.CommittedInstanceID();
	hit.Primitive = query.CommittedPrimitiveIndex();
	if (hit.Instance >= counts.x)
	{
		return GIHitInvalid;
	}
	const uint4 instance = GIInstances[hit.Instance];
	if (instance.x >= counts.y || instance.y >= counts.z || (instance.z & 1u) == 0)
	{
		return GIHitInvalid;
	}
	const uint4 geometry = GIGeometries[instance.x];
	if (hit.Primitive >= geometry.z)
	{
		return GIHitInvalid;
	}
	const uint3 indices = GIIndices.Load3(geometry.y + hit.Primitive * 12);
	if (any(indices >= geometry.w))
	{
		return GIHitInvalid;
	}
	const uint3 addresses = geometry.x + indices * 56;
	const float3 a = asfloat(GIVertices.Load3(addresses.x));
	const float3 b = asfloat(GIVertices.Load3(addresses.y));
	const float3 c = asfloat(GIVertices.Load3(addresses.z));
	const float2 bary = query.CommittedTriangleBarycentrics();
	const float3 weights = float3(1.0 - bary.x - bary.y, bary);
	const float3x4 objectToWorld = query.CommittedObjectToWorld3x4();
	const float3x4 worldToObject = query.CommittedWorldToObject3x4();
	const float3x3 normalMatrix = transpose((float3x3)(worldToObject));
	precise float3 edgeA = b - a;
	precise float3 edgeB = c - a;
	const float3 localNormal = cross(edgeA, edgeB);
	const float3 worldNormal = mul(normalMatrix, localNormal);
	const float normalLengthSquared = dot(worldNormal, worldNormal);
	if (!isfinite(NonFiniteObserveValue(NF_SITE_GI_RAY_SCENE_GITRACE_HIT_ISFINITE, normalLengthSquared)) ||
		normalLengthSquared <= 0.0)
	{
		return GIHitInvalid;
	}
	const float inverseNormalLength = rsqrt(normalLengthSquared);
	hit.FaceNormal = worldNormal * inverseNormalLength;
	precise float3 localPosition = a + (bary.x * edgeA + bary.y * edgeB);
	[unroll]
	for (uint axis = 0; axis < 3; ++axis)
	{
		hit.Position[axis] = objectToWorld[axis][3] + mad(objectToWorld[axis][0], localPosition.x,
			mad(objectToWorld[axis][1], localPosition.y, objectToWorld[axis][2] * localPosition.z));
	}
	// Project reconstruction/intersection/transform error onto the geometric normal,
	// as in the qualified sun-visibility tap; ULP displacement alone is insufficient.
	const float3 span = abs(edgeA) + abs(edgeB) + abs(abs(edgeA) - abs(edgeB));
	float3 localError = 5.96046448e-8 * abs(a) + 1.78813977e-7 * max(span.x, max(span.y, span.z));
	localError += 1.19209318e-7 * mul(abs(worldToObject), float4(abs(hit.Position), 1.0));
	const float3 translation = float3(objectToWorld[0][3], objectToWorld[1][3], objectToWorld[2][3]);
	const float3 worldError = 1.78813977e-7 * mul(abs((float3x3)(objectToWorld)), abs(localPosition)) +
		1.19209318e-7 * abs(translation);
	hit.OriginError = dot(worldError, abs(hit.FaceNormal)) +
		inverseNormalLength * dot(localError, abs(localNormal));
	const float3 normalA = mul(normalMatrix, asfloat(GIVertices.Load3(addresses.x + 12)));
	const float3 normalB = mul(normalMatrix, asfloat(GIVertices.Load3(addresses.y + 12)));
	const float3 normalC = mul(normalMatrix, asfloat(GIVertices.Load3(addresses.z + 12)));
	// Finite zero normals retain the established face-normal fallback. Non-finite
	// inputs must reject the hit before normalization can conceal a deep-path error.
	if (!all(isfinite(NonFiniteObserveValue(NF_SITE_GI_RAY_SCENE_GITRACE_HIT_ISFINITE, normalA))) ||
		!all(isfinite(NonFiniteObserveValue(NF_SITE_GI_RAY_SCENE_GITRACE_HIT_ISFINITE, normalB))) ||
		!all(isfinite(NonFiniteObserveValue(NF_SITE_GI_RAY_SCENE_GITRACE_HIT_ISFINITE, normalC))) ||
		!all(isfinite(NonFiniteObserveValue(NF_SITE_GI_RAY_SCENE_GITRACE_HIT_ISFINITE,
			float4(hit.Position, hit.OriginError)))))
	{
		return GIHitInvalid;
	}
	hit.VertexNormal = SafeNormalize(
		SafeNormalize(normalA, hit.FaceNormal) * weights.x +
		SafeNormalize(normalB, hit.FaceNormal) * weights.y +
		SafeNormalize(normalC, hit.FaceNormal) * weights.z,
		hit.FaceNormal);
	const MaterialData material = GIMaterials[instance.y];
	if (material.AlphaMode != MaterialAlphaModeOpaque)
	{
		return GIHitInvalid;
	}
	if (!query.CommittedTriangleFrontFace())
	{
		if ((material.Flags & 1u) == 0)
		{
			return GIHitBackface;
		}
		hit.FaceNormal = -hit.FaceNormal;
		hit.VertexNormal = -hit.VertexNormal;
	}
	const float2 uv0 = asfloat(GIVertices.Load2(addresses.x + 24)) * weights.x +
		asfloat(GIVertices.Load2(addresses.y + 24)) * weights.y + asfloat(GIVertices.Load2(addresses.z + 24)) * weights.z;
	const float2 uv1 = asfloat(GIVertices.Load2(addresses.x + 32)) * weights.x +
		asfloat(GIVertices.Load2(addresses.y + 32)) * weights.y + asfloat(GIVertices.Load2(addresses.z + 32)) * weights.z;
	if (!all(isfinite(NonFiniteObserveValue(NF_SITE_GI_RAY_SCENE_GITRACE_HIT_ISFINITE, float4(uv0, uv1)))))
	{
		return GIHitInvalid;
	}
	hit.Albedo = EvaluateGIHitAlbedo(material, uv0, uv1);
	if (!all(isfinite(NonFiniteObserveValue(NF_SITE_GI_RAY_SCENE_GITRACE_HIT_ISFINITE, hit.Albedo))))
	{
		return GIHitInvalid;
	}
	return GIHitSurface;
}

bool GISunVisible(GIHit hit, float3 direction, float maximumDistance)
{
	if (dot(hit.FaceNormal, direction) <= 0.0)
	{
		return false;
	}
	RayDesc ray;
	ray.Origin = GIHitOrigin(hit);
	ray.Direction = direction;
	ray.TMin = 0.0;
	ray.TMax = maximumDistance;
	RayQuery<RAY_FLAG_FORCE_OPAQUE | RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH> query;
	query.TraceRayInline(GIRayScene, RAY_FLAG_NONE, 1, ray);
	while (query.Proceed())
	{
	}
	return query.CommittedStatus() == COMMITTED_NOTHING;
}
