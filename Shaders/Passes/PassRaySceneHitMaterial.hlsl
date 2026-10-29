#include <Common/Common.hlsli>
#include <Common/ApplicationBinding.hlsli>
#include <Common/DepthReconstruction.hlsli>
#include <Common/VertexTransform.hlsli>
#include <Lighting/GIHitMaterial.hlsli>

RaytracingAccelerationStructure Scene : register(t5);
ByteAddressBuffer Vertices : register(t6);
ByteAddressBuffer Indices : register(t7);
StructuredBuffer<uint4> Geometries : register(t8);
StructuredBuffer<uint4> Instances : register(t9);
StructuredBuffer<MaterialData> Materials : register(t10);
cbuffer Parameters : register(b2)
{
	uint DepthIndex;
	uint AlbedoIndex;
	uint NormalIndex;
	uint IdentityIndex;
	uint ViewIndex;
	uint Width;
	uint Height;
	uint InstanceCount;
	uint GeometryCount;
	uint MaterialCount;
	uint ReceiverIndex;
	uint Padding;
};

float3 TransformHitNormal(float3 localNormal, float3x3 normalMatrix)
{
	return SafeNormalize(mul(normalMatrix, localNormal), float3(0.0, 1.0, 0.0));
}

[numthreads(8, 8, 1)]
void CSMain(uint3 id : SV_DispatchThreadID)
{
	const uint2 pixel = id.xy;
	if (any(pixel >= uint2(Width, Height)))
	{
		return;
	}
	RWTexture2D<float4> albedoOutput = GetRWTexture2DFloat4(AlbedoIndex);
	RWTexture2D<float4> normalOutput = GetRWTexture2DFloat4(NormalIndex);
	RWTexture2D<float4> identityOutput = GetRWTexture2DFloat4(IdentityIndex);
	albedoOutput[pixel] = 0.0.xxxx;
	normalOutput[pixel] = 0.0.xxxx;
	identityOutput[pixel] = -1.0.xxxx;
	const ViewData view = LoadViewData(ViewIndex);
	Texture2D<float> depth = GetTexture2DFloat(DepthIndex);
	const float rawDepth = depth.Load(int3(pixel, 0));
	Texture2D<float4> receivers = GetTexture2DFloat4(ReceiverIndex);
	const float receiverClass = receivers.Load(int3(pixel, 0)).w;
	if (IsDepthBackground(rawDepth, view.DepthConvention))
	{
		return;
	}
	if (receiverClass != 1.0)
	{
		albedoOutput[pixel] = float4(0.0, 0.0, 0.0, -2.0);
		return;
	}
	const float3 positionVS = ReconstructViewPosition(PixelCenterToUV(pixel, uint2(Width, Height)), rawDepth, view.InvProjMat);
	const float3 position = mul(float4(positionVS, 1.0), view.InvViewMat).xyz;
	const float distance = length(position - view.CameraPos.xyz);
	const float tolerance = max(0.002, distance * 0.0002);
	RayDesc ray;
	ray.Origin = view.CameraPos.xyz;
	ray.Direction = normalize(position - ray.Origin);
	ray.TMin = max(0.001, distance - tolerance);
	ray.TMax = distance + tolerance;
	RayQuery<RAY_FLAG_FORCE_OPAQUE> query;
	query.TraceRayInline(Scene, RAY_FLAG_NONE, 1, ray);
	while (query.Proceed())
	{
	}
	if (query.CommittedStatus() != COMMITTED_TRIANGLE_HIT)
	{
		albedoOutput[pixel] = float4(0.0, 0.0, 0.0, -1.0);
		return;
	}
	// Counts come from the same immutable snapshot as these fixed root SRVs.
	albedoOutput[pixel] = float4(0.0, 0.0, 0.0, -2.0);
	const uint instanceId = query.CommittedInstanceID();
	const uint primitiveId = query.CommittedPrimitiveIndex();
	if (instanceId >= InstanceCount)
	{
		return;
	}
	const uint4 instance = Instances[instanceId];
	if (instance.x >= GeometryCount || instance.y >= MaterialCount || (instance.z & 1u) == 0)
	{
		return;
	}
	const uint4 geometry = Geometries[instance.x];
	if (primitiveId >= geometry.z)
	{
		return;
	}
	const uint3 indices = Indices.Load3(geometry.y + primitiveId * 12);
	if (any(indices >= geometry.w))
	{
		return;
	}
	const uint3 addresses = geometry.x + indices * 56;
	const float2 bary = query.CommittedTriangleBarycentrics();
	const float3 weights = float3(1.0 - bary.x - bary.y, bary);
	const float2 uv0 = asfloat(Vertices.Load2(addresses.x + 24)) * weights.x +
		asfloat(Vertices.Load2(addresses.y + 24)) * weights.y + asfloat(Vertices.Load2(addresses.z + 24)) * weights.z;
	const float2 uv1 = asfloat(Vertices.Load2(addresses.x + 32)) * weights.x +
		asfloat(Vertices.Load2(addresses.y + 32)) * weights.y + asfloat(Vertices.Load2(addresses.z + 32)) * weights.z;
	const float3x3 normalMatrix = transpose((float3x3)(query.CommittedWorldToObject3x4()));
	// Match Forward coverage: normalize each transformed vertex before interpolation.
	float3 normal = SafeNormalize(
		TransformHitNormal(asfloat(Vertices.Load3(addresses.x + 12)), normalMatrix) * weights.x +
		TransformHitNormal(asfloat(Vertices.Load3(addresses.y + 12)), normalMatrix) * weights.y +
		TransformHitNormal(asfloat(Vertices.Load3(addresses.z + 12)), normalMatrix) * weights.z,
		float3(0.0, 1.0, 0.0));
	const MaterialData material = Materials[instance.y];
	if (material.AlphaMode != MaterialAlphaModeOpaque)
	{
		return;
	}
	const bool frontFace = query.CommittedTriangleFrontFace();
	const bool doubleSided = (material.Flags & 1u) != 0;
	if (doubleSided && !frontFace)
	{
		normal = -normal;
	}
	// A single-sided backface is an explicit hit, never an environment miss.
	albedoOutput[pixel] = float4(EvaluateGIHitAlbedo(material, uv0, uv1), !frontFace && !doubleSided ? 2.0 : 1.0);
	normalOutput[pixel] = float4(normal, frontFace ? 1.0 : -1.0);
	identityOutput[pixel] = float4((float)(instanceId), (float)(primitiveId), (float)(instance.y), (float)(instance.x));
}
