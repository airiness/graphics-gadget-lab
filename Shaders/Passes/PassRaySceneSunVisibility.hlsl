#include <Common/Common.hlsli>
#include <Common/ApplicationBinding.hlsli>
#include <Common/BindlessResources.hlsli>
#include <Common/DepthReconstruction.hlsli>
#include <Lighting/DirectionalShadowVisibility.hlsli>

RaytracingAccelerationStructure Scene : register(t5);
ByteAddressBuffer Vertices : register(t6);
ByteAddressBuffer Indices : register(t7);
StructuredBuffer<uint4> Geometries : register(t8);
StructuredBuffer<uint4> Instances : register(t9);
ConstantBuffer<DirectionalShadowData> Shadow : register(b3);
cbuffer Parameters : register(b2)
{
	uint DepthIndex;
	uint OutputIndex;
	uint ShadowIndex;
	uint ShadowSampler;
	uint ViewIndex;
	uint Width;
	uint Height;
	uint ShadowSize;
	uint ShadowFlags;
	float3 SunDirection;
	uint InstanceCount;
	uint GeometryCount;
	uint2 Padding;
};

float3 ViewRay(uint2 pixel, ViewData view)
{
	const float3 positionVS = ReconstructViewPosition(PixelCenterToUV(pixel, uint2(Width, Height)),
		GetDepthNearValue(view.DepthConvention), view.InvProjMat);
	return normalize(mul(float4(positionVS, 0.0), view.InvViewMat).xyz);
}

// Ray Tracing Gems, chapter 6: normal-directed ULP offset, with a small-origin fallback.
float3 OffsetOrigin(float3 position, float3 normal)
{
	const int3 offset = (int3)(256.0 * normal);
	const int3 bits = asint(position) + int3(position.x < 0.0 ? -offset.x : offset.x,
		position.y < 0.0 ? -offset.y : offset.y, position.z < 0.0 ? -offset.z : offset.z);
	const float3 shifted = asfloat(bits);
	return float3(abs(position.x) < 0.03125 ? position.x + normal.x / 65536.0 : shifted.x,
		abs(position.y) < 0.03125 ? position.y + normal.y / 65536.0 : shifted.y,
		abs(position.z) < 0.03125 ? position.z + normal.z / 65536.0 : shifted.z);
}

// RTX intersection and transform error bounds, projected onto the triangle normal.
// Large triangles can self-intersect even when the world-position ULP offset is tiny.
// https://developer.nvidia.com/blog/solving-self-intersection-artifacts-in-directx-raytracing/
float TriangleOriginError(float3 vertex, float3 edgeA, float3 edgeB, float3 localPosition,
	float3 worldPosition, float3 localNormal, float3 worldNormal, float inverseNormalLength,
	float3x4 objectToWorld, float3x4 worldToObject)
{
	const float reconstructionError = 5.96046448e-8;
	const float intersectionError = 1.78813977e-7;
	const float transformError = 1.19209318e-7;
	const float3 span = abs(edgeA) + abs(edgeB) + abs(abs(edgeA) - abs(edgeB));
	const float maximumSpan = max(span.x, max(span.y, span.z));
	float3 localError = reconstructionError * abs(vertex) + intersectionError * maximumSpan;
	localError += transformError * mul(abs(worldToObject), float4(abs(worldPosition), 1.0));
	const float3 translation = float3(objectToWorld[0][3], objectToWorld[1][3], objectToWorld[2][3]);
	const float3 worldError = intersectionError * mul(abs((float3x3)(objectToWorld)), abs(localPosition)) +
		transformError * abs(translation);
	return dot(worldError, abs(worldNormal)) + inverseNormalLength * dot(localError, abs(localNormal));
}

float3 PlanePosition(float3 origin, float3 direction, float3 surfacePosition, float3 normal)
{
	return origin + direction * (dot(surfacePosition - origin, normal) / dot(direction, normal));
}

[numthreads(8, 8, 1)]
void CSMain(uint3 id : SV_DispatchThreadID)
{
	const uint2 pixel = id.xy;
	if (any(pixel >= uint2(Width, Height)))
	{
		return;
	}
	RWTexture2D<float4> output = GetRWTexture2DFloat4(OutputIndex);
	output[pixel] = float4(0.0, 0.0, 0.0, -1.0);
	const ViewData view = LoadViewData(ViewIndex);
	Texture2D<float> depth = GetTexture2DFloat(DepthIndex);
	const float rawDepth = depth.Load(int3(pixel, 0));
	if (IsDepthBackground(rawDepth, view.DepthConvention))
	{
		return;
	}
	const float3 positionVS = ReconstructViewPosition(PixelCenterToUV(pixel, uint2(Width, Height)), rawDepth, view.InvProjMat);
	const float3 position = mul(float4(positionVS, 1.0), view.InvViewMat).xyz;
	const float3 direction = normalize(position - view.CameraPos.xyz);
	const float distance = length(position - view.CameraPos.xyz);
	const float tolerance = max(0.002, distance * 0.0002);
	// Resolve the triangle at opaque raster depth; earlier back faces need not rasterize.
	RayDesc primary;
	primary.Origin = view.CameraPos.xyz;
	primary.Direction = direction;
	primary.TMin = max(0.001, distance - tolerance);
	primary.TMax = distance + tolerance;
	RayQuery<RAY_FLAG_FORCE_OPAQUE> receiver;
	receiver.TraceRayInline(Scene, RAY_FLAG_NONE, 1, primary);
	while (receiver.Proceed())
	{
	}
	if (receiver.CommittedStatus() != COMMITTED_TRIANGLE_HIT)
	{
		// Explicit unmatched receiver, retained in exports and agreement statistics.
		output[pixel] = float4(0.0, 0.0, -1.0, -1.0);
		return;
	}
	// Fixed root SRVs carry addresses, not element counts. Use this snapshot's logical ranges.
	if (receiver.CommittedInstanceID() >= InstanceCount)
	{
		output[pixel] = float4(0.0, 0.0, -2.0, -1.0);
		return;
	}
	const uint4 instance = Instances[receiver.CommittedInstanceID()];
	if (instance.x >= GeometryCount || (instance.z & 1u) == 0)
	{
		output[pixel] = float4(0.0, 0.0, -2.0, -1.0);
		return;
	}
	const uint4 geometry = Geometries[instance.x];
	if (receiver.CommittedPrimitiveIndex() >= geometry.z)
	{
		output[pixel] = float4(0.0, 0.0, -2.0, -1.0);
		return;
	}
	const uint3 indices = Indices.Load3(geometry.y + receiver.CommittedPrimitiveIndex() * 12);
	if (any(indices >= geometry.w))
	{
		output[pixel] = float4(0.0, 0.0, -2.0, -1.0);
		return;
	}
	const float3 a = asfloat(Vertices.Load3(geometry.x + indices.x * 56));
	const float3 b = asfloat(Vertices.Load3(geometry.x + indices.y * 56));
	const float3 c = asfloat(Vertices.Load3(geometry.x + indices.z * 56));
	const float3x4 objectToWorld = receiver.CommittedObjectToWorld3x4();
	const float3x4 worldToObject = receiver.CommittedWorldToObject3x4();
	precise float3 edge1 = b - a;
	precise float3 edge2 = c - a;
	const float3 localNormal = cross(edge1, edge2);
	const float3 transformedNormal = mul(transpose((float3x3)(worldToObject)), localNormal);
	const float inverseNormalLength = rsqrt(dot(transformedNormal, transformedNormal));
	const float3 normal = transformedNormal * inverseNormalLength;
	// Reconstruct on the triangle, rather than accumulating camera-origin and ray-t error.
	const float2 barycentrics = receiver.CommittedTriangleBarycentrics();
	precise float3 localPosition = a + (barycentrics.x * edge1 + barycentrics.y * edge2);
	precise float3 surfacePosition;
	[unroll]
	for (uint axis = 0; axis < 3; ++axis)
	{
		// Add translation last so its rounding error does not enter the linear transform.
		surfacePosition[axis] = objectToWorld[axis][3] + mad(objectToWorld[axis][0], localPosition.x,
			mad(objectToWorld[axis][1], localPosition.y, objectToWorld[axis][2] * localPosition.z));
	}
	const float3 lightDirection = normalize(SunDirection);
	const float3 offsetNormal = dot(normal, lightDirection) >= 0.0 ? normal : -normal;
	const float originError = TriangleOriginError(a, edge1, edge2, localPosition, surfacePosition,
		localNormal, normal, inverseNormalLength, objectToWorld, worldToObject);
	RayDesc sun;
	sun.Origin = OffsetOrigin(surfacePosition + originError * offsetNormal, offsetNormal);
	sun.Direction = lightDirection;
	sun.TMin = 0.0;
	sun.TMax = 1000.0;
	RayQuery<RAY_FLAG_FORCE_OPAQUE | RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH> query;
	query.TraceRayInline(Scene, RAY_FLAG_NONE, 1, sun);
	while (query.Proceed())
	{
	}
	const bool blocked = query.CommittedStatus() == COMMITTED_TRIANGLE_HIT;
	const uint2 quad = pixel & ~1u;
	ShadowReceiverPlane plane;
	// Analytic triangle-plane derivatives avoid depth-neighbour discontinuities.
	plane.PositionDx = PlanePosition(primary.Origin, ViewRay(uint2(quad.x + 1, pixel.y), view), surfacePosition, normal) -
		PlanePosition(primary.Origin, ViewRay(uint2(quad.x, pixel.y), view), surfacePosition, normal);
	plane.PositionDy = PlanePosition(primary.Origin, ViewRay(uint2(pixel.x, quad.y + 1), view), surfacePosition, normal) -
		PlanePosition(primary.Origin, ViewRay(uint2(pixel.x, quad.y), view), surfacePosition, normal);
	plane.NormalWS = normal;
	Texture2DArray<float> shadowMap = GetTexture2DArrayFloat(ShadowIndex);
	SamplerComparisonState samplerState = GetSamplerComparisonState(ShadowSampler);
	const float csm = (ShadowFlags & 1u) == 0 ? 1.0 :
		EvaluateDirectionalShadowVisibility(surfacePosition, plane, dot(normal, lightDirection), Shadow,
			shadowMap, samplerState, ShadowSize, (ShadowFlags & 2u) != 0);
	// R: binary RT visibility, G: filtered CSM visibility, B: receiver state,
	// A: first accepted occluder distance in metres, -1 for a miss (not closest-hit t).
	output[pixel] = float4(blocked ? 0.0 : 1.0, csm, 1.0, blocked ? query.CommittedRayT() : -1.0);
}
