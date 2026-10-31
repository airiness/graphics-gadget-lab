#pragma once
#include <Lighting/GIShaderConstants.hlsli>

// Ray Tracing Gems, chapter 6: normal-directed ULP offset with a small-origin fallback.
float3 OffsetRaySceneOrigin(float3 position, float3 normal)
{
	const float ulpScale = 256.0;
	const float smallOrigin = 1.0 / 32.0;
	const float smallOriginScale = 1.0 / 65536.0;
	const int3 offset = (int3)(ulpScale * normal);
	const int3 bits = asint(position) + int3(position.x < 0.0 ? -offset.x : offset.x,
		position.y < 0.0 ? -offset.y : offset.y, position.z < 0.0 ? -offset.z : offset.z);
	const float3 shifted = asfloat(bits);
	return float3(abs(position.x) < smallOrigin ? position.x + normal.x * smallOriginScale : shifted.x,
		abs(position.y) < smallOrigin ? position.y + normal.y * smallOriginScale : shifted.y,
		abs(position.z) < smallOrigin ? position.z + normal.z * smallOriginScale : shifted.z);
}

// RTX intersection and transform error bounds, projected onto the triangle normal.
// https://developer.nvidia.com/blog/solving-self-intersection-artifacts-in-directx-raytracing/
float RaySceneTriangleOriginError(float3 vertex, float3 edgeA, float3 edgeB, float3 localPosition,
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

// Primary raster-depth lookup tolerances, in world units and relative camera distance.
static const float RaySceneMinimumMatchTolerance = 0.002;
static const float RaySceneRelativeMatchTolerance = 0.0002;
static const float RayScenePrimaryMinimumDistance = 0.001;
static const float RaySceneMatchingNormalCosine = 0.9999;
