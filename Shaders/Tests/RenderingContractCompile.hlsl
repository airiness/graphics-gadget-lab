#include <Common/DepthReconstruction.hlsli>

RWStructuredBuffer<float4> g_ContractOutput : register(u0);

[numthreads(1, 1, 1)]
void CSMain(uint3 dispatchThreadId : SV_DispatchThreadID)
{
	const float4x4 identity = float4x4(
		1.0, 0.0, 0.0, 0.0,
		0.0, 1.0, 0.0, 0.0,
		0.0, 0.0, 1.0, 0.0,
		0.0, 0.0, 0.0, 1.0);
	const float2 uv = PixelCenterToUV(dispatchThreadId.xy, uint2(1, 1));
	const float2 roundTripUV = NDCToUV(UVToNDC(uv));
	const float3 positionVS = ReconstructViewPosition(uv, 0.5, identity);
	const float3 positionWS = ReconstructWorldPosition(uv, 0.5, identity);
	const float viewZ = RawDepthToPositiveViewZ(uv, 0.5, identity);
	const bool conventionChecks =
		GetDepthNearValue(DEPTH_CONVENTION_REVERSED) == 1.0 &&
		GetDepthFarValue(DEPTH_CONVENTION_REVERSED) == 0.0 &&
		GetDepthNearValue(DEPTH_CONVENTION_STANDARD) == 0.0 &&
		GetDepthFarValue(DEPTH_CONVENTION_STANDARD) == 1.0 &&
		IsDepthBackground(0.0, DEPTH_CONVENTION_REVERSED, 0.0) &&
		IsDepthBackground(1.0, DEPTH_CONVENTION_STANDARD, 0.0) &&
		IsDepthNearer(0.75, 0.25, DEPTH_CONVENTION_REVERSED) &&
		IsDepthFarther(0.75, 0.25, DEPTH_CONVENTION_STANDARD);

	g_ContractOutput[0] = float4(
		positionVS.x + positionWS.x + roundTripUV.x,
		positionVS.y + positionWS.y + roundTripUV.y,
		viewZ,
		conventionChecks ? 1.0 : 0.0);
}
