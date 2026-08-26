#include <Common/BufferLayout.hlsli>

static const uint ViewDataSize = sizeof(ViewData);

StructuredBuffer<ViewData> g_ContractViews : register(t0);
RWStructuredBuffer<float4> g_ContractOutput : register(u0);

[numthreads(1, 1, 1)]
void CSMain(uint3 dispatchThreadId : SV_DispatchThreadID)
{
	const ViewData view = g_ContractViews[dispatchThreadId.x];
	g_ContractOutput[0] = view.CameraPos +
		view.PreviousDepthReconstructionParams +
		float4(view.CurrentJitterUV, view.PreviousJitterUV) +
		float4(view.PreviousRasterViewProj[0].xy, ViewDataSize, view.PreviousDepthConvention);
}
