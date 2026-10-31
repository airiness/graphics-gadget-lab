#pragma once
#include <Lighting/GIHitMaterial.hlsli>
#include <Lighting/RaySceneGeometry.hlsli>

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

float3 GIHitOrigin(GIHit hit)
{
	return OffsetRaySceneOrigin(hit.Position + hit.OriginError * hit.FaceNormal, hit.FaceNormal);
}

// Counts and bindings must describe the same immutable all-World snapshot.
// Secondary rays never cull backfaces: a single-sided backface is a zero-contribution
// HIT, not sky. A primary lookup instead matches raster sidedness, including at the
// coincident internal faces of touching closed solids.
uint GITraceHit(RayDesc ray, uint3 counts, out GIHit hit, bool rasterReceiver)
{
	hit = (GIHit)(0);
	RayQuery<RAY_FLAG_NONE> query;
	query.TraceRayInline(GIRayScene, rasterReceiver ? RAY_FLAG_FORCE_NON_OPAQUE : RAY_FLAG_FORCE_OPAQUE,
		RaySceneOpaqueMask, ray);
	while (query.Proceed())
	{
		if (query.CandidateType() != CANDIDATE_NON_OPAQUE_TRIANGLE ||
			query.CandidateInstanceID() >= counts.x)
		{
			return GIHitInvalid;
		}
		const uint4 candidate = GIInstances[query.CandidateInstanceID()];
		if (candidate.y >= counts.z || (candidate.z & RaySceneOpaqueMask) == 0)
		{
			return GIHitInvalid;
		}
		if (query.CandidateTriangleFrontFace() || (GIMaterials[candidate.y].Flags & MaterialFlagDoubleSided) != 0)
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
	if (instance.x >= counts.y || instance.y >= counts.z || (instance.z & RaySceneOpaqueMask) == 0)
	{
		return GIHitInvalid;
	}
	const uint4 geometry = GIGeometries[instance.x];
	if (hit.Primitive >= geometry.z)
	{
		return GIHitInvalid;
	}
	const uint3 indices = GIIndices.Load3(geometry.y + hit.Primitive * RaySceneTriangleIndexBytes);
	if (any(indices >= geometry.w))
	{
		return GIHitInvalid;
	}
	const uint3 addresses = geometry.x + indices * RaySceneVertexStride;
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
	hit.OriginError = RaySceneTriangleOriginError(a, edgeA, edgeB, localPosition, hit.Position,
		localNormal, hit.FaceNormal, inverseNormalLength, objectToWorld, worldToObject);
	const float3 normalA = mul(normalMatrix, asfloat(GIVertices.Load3(addresses.x + RaySceneNormalOffset)));
	const float3 normalB = mul(normalMatrix, asfloat(GIVertices.Load3(addresses.y + RaySceneNormalOffset)));
	const float3 normalC = mul(normalMatrix, asfloat(GIVertices.Load3(addresses.z + RaySceneNormalOffset)));
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
		if ((material.Flags & MaterialFlagDoubleSided) == 0)
		{
			return GIHitBackface;
		}
		hit.FaceNormal = -hit.FaceNormal;
		hit.VertexNormal = -hit.VertexNormal;
	}
	const float2 uv0 = asfloat(GIVertices.Load2(addresses.x + RaySceneUV0Offset)) * weights.x +
		asfloat(GIVertices.Load2(addresses.y + RaySceneUV0Offset)) * weights.y +
		asfloat(GIVertices.Load2(addresses.z + RaySceneUV0Offset)) * weights.z;
	const float2 uv1 = asfloat(GIVertices.Load2(addresses.x + RaySceneUV1Offset)) * weights.x +
		asfloat(GIVertices.Load2(addresses.y + RaySceneUV1Offset)) * weights.y +
		asfloat(GIVertices.Load2(addresses.z + RaySceneUV1Offset)) * weights.z;
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

bool GIReceiverMatched(uint match)
{
	return match == GIReceiverMatchCenter || match == GIReceiverMatchEnvelope ||
		match == GIReceiverMatchRaster;
}

// Only primary matching follows raster coverage. Secondary rays keep their
// original hit/miss and sidedness rules, including occluded single-sided backfaces.
uint GIMatchReceiver(uint2 pixel, uint2 size, float3 position, float3 rasterPosition,
	float3 face, ViewData view, float maximumDistance, uint3 counts, out GIHit receiver)
{
	receiver = (GIHit)(0);
	const float3 delta = position - view.CameraPos.xyz;
	const float tolerance = max(RaySceneMinimumMatchTolerance,
		length(delta) * RaySceneRelativeMatchTolerance);
	const float3 positionVS = mul(float4(position, 1), view.ViewMat).xyz;
	const float footprint = 2.0 * abs(positionVS.z * view.InvProjMat[1][1]) / (float)(size.y);
	const float pixelRadius = max(1.0 / 128.0, 2.0e-6 / max(footprint, 1.0e-20));
	const uint envelopeRadius = 4;
	const uint envelopeWidth = 2 * envelopeRadius + 1;
	const uint matchAttempts = 1 + envelopeWidth * envelopeWidth;
	uint failure = GIReceiverMatchMiss;
	for (uint attempt = 0; attempt < matchAttempts; ++attempt)
	{
		// A thin triangle need not intersect the envelope's corners or axes.
		float2 offset = 0.0.xx;
		if (attempt != 0)
		{
			const uint index = attempt - 1;
			offset = (float2(index % envelopeWidth, index / envelopeWidth) -
				(float)(envelopeRadius)) / (float)(envelopeRadius);
		}
		const float2 uv = ((float2)(pixel) + 0.5 + pixelRadius * offset) / (float2)(size);
		const float3 nearVS = ReconstructViewPosition(uv,
			GetDepthNearValue(view.DepthConvention), view.InvProjMat);
		RayDesc ray;
		ray.Origin = view.CameraPos.xyz;
		ray.Direction = normalize(mul(float4(nearVS, 0), view.InvViewMat).xyz);
		const float cosine = dot(ray.Direction, face);
		if (abs(cosine) <= 1.0e-8)
		{
			continue;
		}
		// Depth uncertainty is a face-plane displacement. Project it onto the
		// ray instead of clipping valid grazing receivers with a fixed t window.
		const float planeDistance = dot(delta, face) / cosine;
		const float rayTolerance = tolerance / abs(cosine);
		ray.TMin = max(RayScenePrimaryMinimumDistance, planeDistance - rayTolerance);
		ray.TMax = min(maximumDistance, planeDistance + rayTolerance);
		if (ray.TMin >= ray.TMax)
		{
			continue;
		}
		GIHit candidate;
		const uint outcome = GITraceHit(ray, counts, candidate, true);
		if (outcome != GIHitMiss && outcome != GIHitSurface)
		{
			return GIReceiverMatchInvalid;
		}
		if (outcome == GIHitSurface)
		{
			failure = GIReceiverMatchDifferentSurface;
			if (dot(candidate.FaceNormal, face) > RaySceneMatchingNormalCosine &&
				abs(dot(candidate.Position - position, face)) <= tolerance)
			{
				receiver = candidate;
				return attempt == 0 ? GIReceiverMatchCenter : GIReceiverMatchEnvelope;
			}
		}
	}
	// Fixed-point raster coverage/interpolation can differ from an ideal centre
	// ray at a grazing sliver. Resolve the paired raster interpolant locally on
	// the immutable triangles; require the same face and its reconstruction bound.
	// This is an explicit primary match kind, never a replacement environment miss.
	if (abs(dot(rasterPosition - position, face)) <= tolerance)
	{
		RayDesc local;
		local.Origin = rasterPosition + tolerance * face;
		local.Direction = -face;
		local.TMin = 0;
		local.TMax = 2.0 * tolerance;
		GIHit candidate;
		const uint outcome = GITraceHit(local, counts, candidate, true);
		const float magnitude = max(abs(rasterPosition.x),
			max(abs(rasterPosition.y), abs(rasterPosition.z)));
		const float error = max(2.0e-6, 4.0 * candidate.OriginError +
			16.0 * 1.1920928955078125e-7 * magnitude);
		if (outcome == GIHitSurface && dot(candidate.FaceNormal, face) > RaySceneMatchingNormalCosine &&
			length(candidate.Position - rasterPosition) <= error)
		{
			receiver = candidate;
			return GIReceiverMatchRaster;
		}
	}
	return failure;
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
	query.TraceRayInline(GIRayScene, RAY_FLAG_NONE, RaySceneOpaqueMask, ray);
	while (query.Proceed())
	{
	}
	return query.CommittedStatus() == COMMITTED_NOTHING;
}
