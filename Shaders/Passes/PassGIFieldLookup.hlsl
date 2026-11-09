#include <Common/Common.hlsli>
#include <Lighting/GIFieldSampling.hlsli>

cbuffer Parameters : register(b2)
{
	GIFieldSamplingParameters Field;
	uint PositionIndex;
	uint VertexNormalIndex;
	uint ShadingNormalIndex;
	uint IrradianceOutput;
	uint QueryOutput;
	uint2 Extent;
	uint Padding;
	float3 CameraPosition;
	uint CameraPadding;
};

[numthreads(GIQueryGroupWidth, GIQueryGroupHeight, 1)]
void CSMain(uint3 id : SV_DispatchThreadID)
{
	if (any(id.xy >= Extent))
	{
		return;
	}
	Texture2D<float4> positions = ResourceDescriptorHeap[NonUniformResourceIndex(PositionIndex)];
	Texture2D<float4> vertexNormals = ResourceDescriptorHeap[NonUniformResourceIndex(VertexNormalIndex)];
	Texture2D<float4> shadingNormals = ResourceDescriptorHeap[NonUniformResourceIndex(ShadingNormalIndex)];
	RWTexture2D<float4> irradiance = ResourceDescriptorHeap[NonUniformResourceIndex(IrradianceOutput)];
	RWTexture2D<float4> queries = ResourceDescriptorHeap[NonUniformResourceIndex(QueryOutput)];
	const float4 position = positions.Load(int3(id.xy, 0));
	if (position.a != (float)(GIReceiverOpaque))
	{
		irradiance[id.xy] = 0.0.xxxx;
		queries[id.xy] = 0.0.xxxx;
		return;
	}
	const float3 shadingNormal = shadingNormals.Load(int3(id.xy, 0)).xyz;
	const float3 vertexNormal = vertexNormals.Load(int3(id.xy, 0)).xyz;
	const float3 viewDirection = normalize(CameraPosition - position.xyz);
	const GIFieldQuery result = SampleGIField(Field, position.xyz, shadingNormal, vertexNormal, viewDirection);
	const float marker = result.Compatible != 0 ? 1.0 : -1.0;
	irradiance[id.xy] = float4(result.Irradiance, marker);
	queries[id.xy] = float4(result.Weight, result.Coverage, (float)(result.Support), marker);
}
