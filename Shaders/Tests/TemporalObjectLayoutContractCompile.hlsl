#include <Common/BufferLayout.hlsli>

static const uint ObjectDataSize = sizeof(ObjectData);

StructuredBuffer<ObjectData> g_ContractObjects : register(t0);
RWStructuredBuffer<float4> g_ContractOutput : register(u0);

[numthreads(1, 1, 1)]
void CSMain(uint3 dispatchThreadId : SV_DispatchThreadID)
{
	const ObjectData objectData = g_ContractObjects[dispatchThreadId.x];
	g_ContractOutput[0] = objectData.ModelMat[0] + objectData.PreviousModelMat[0] +
		objectData.NormalMat[0] +
		float4(objectData.MaterialIndex, objectData.ViewIndex, objectData.Padding.x,
			ObjectDataSize);
}
