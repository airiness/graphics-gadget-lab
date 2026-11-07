#include <Common/Common.hlsli>
#include <Lighting/GIFieldSampling.hlsli>

// Match GIReceiverListLimits in GGLabRuntime/Graphics/Pipeline/GIField.h.
static const uint GIReceiverListMaximumSamples = 64;
static const uint GIReceiverListMaximumViewers = 4;

// Matches ReceiverListParameters in Graphics/Pipeline/GIField.cpp.
cbuffer Parameters : register(b2)
{
	GIFieldSamplingParameters Field;
	uint SampleCount;
	uint ViewerCount;
	uint OutputIndex;
	uint Padding;
	float4 Viewers[GIReceiverListMaximumViewers];
	float4 Positions[GIReceiverListMaximumSamples];
	float4 ShadingNormals[GIReceiverListMaximumSamples];
	float4 VertexNormals[GIReceiverListMaximumSamples];
};

// One column per sample. Rows 0-2 record the inputs used; each viewer adds its
// position, the field irradiance and the query record of the raster lookup.
[numthreads(64, 1, 1)]
void CSMain(uint3 id : SV_DispatchThreadID)
{
	if (id.x >= SampleCount || id.y >= ViewerCount)
	{
		return;
	}
	RWTexture2D<float4> output = ResourceDescriptorHeap[NonUniformResourceIndex(OutputIndex)];
	const float3 position = Positions[id.x].xyz;
	const float3 shadingNormal = ShadingNormals[id.x].xyz;
	const float3 vertexNormal = VertexNormals[id.x].xyz;
	if (id.y == 0)
	{
		output[uint2(id.x, 0)] = float4(position, 1.0);
		output[uint2(id.x, 1)] = float4(shadingNormal, 0.0);
		output[uint2(id.x, 2)] = float4(vertexNormal, 0.0);
	}
	const float3 viewer = Viewers[id.y].xyz;
	const float3 viewDirection = normalize(viewer - position);
	const GIFieldQuery result = SampleGIField(Field, position, shadingNormal, vertexNormal, viewDirection);
	const float marker = result.Compatible != 0 ? 1.0 : -1.0;
	const uint row = 3 + 3 * id.y;
	output[uint2(id.x, row)] = float4(viewer, (float)(id.y));
	output[uint2(id.x, row + 1)] = float4(result.Irradiance, marker);
	output[uint2(id.x, row + 2)] = float4(result.Weight, result.Coverage, (float)(result.Support), marker);
}
