#include <Common/Common.hlsli>
#include <Common/ApplicationBinding.hlsli>
#include <Common/BindlessResources.hlsli>
#include <Lighting/GTAO.hlsli>

struct GTAOEvaluatePassParameters
{
	uint DepthTextureIndex;
	uint RawAOUavIndex;
	uint HalfDepthUavIndex;
	uint NormalUavIndex;
	uint SelectedOffsetUavIndex;
	uint ViewIndex;
	uint FullWidth;
	uint FullHeight;
	uint HalfWidth;
	uint HalfHeight;
	uint DirectionCount;
	uint StepCount;
	float Radius;
	float FalloffStart;
	float FalloffEnd;
	float Thickness;
};

ConstantBuffer<GTAOEvaluatePassParameters> g_Pass : register(b2);

[numthreads(8, 8, 1)]
void CSMain(uint3 dispatchThreadId : SV_DispatchThreadID)
{
	const uint2 halfPixel = dispatchThreadId.xy;
	if (any(halfPixel >= uint2(g_Pass.HalfWidth, g_Pass.HalfHeight)))
	{
		return;
	}

	Texture2D<float> depthTexture = GetTexture2DFloat(g_Pass.DepthTextureIndex);
	RWTexture2D<float> rawAO = GetRWTexture2DFloat(g_Pass.RawAOUavIndex);
	RWTexture2D<float> halfDepth = GetRWTexture2DFloat(g_Pass.HalfDepthUavIndex);
	RWTexture2D<float4> reconstructedNormal = GetRWTexture2DFloat4(g_Pass.NormalUavIndex);
	RWTexture2D<float2> selectedOffset = GetRWTexture2DFloat2(g_Pass.SelectedOffsetUavIndex);
	const uint viewIndex = g_Scene.ViewBaseIndex + g_Pass.ViewIndex;
	const ViewData viewData = g_Views[viewIndex];
	const GTAOSurface surface = LoadHalfResolutionSurface(depthTexture, halfPixel,
		uint2(g_Pass.FullWidth, g_Pass.FullHeight), viewData, g_Pass.Radius);

	if (!surface.IsValid)
	{
		rawAO[halfPixel] = 1.0;
		halfDepth[halfPixel] = 0.0;
		reconstructedNormal[halfPixel] = 0.0.xxxx;
		selectedOffset[halfPixel] = -1.0.xx;
		return;
	}

	rawAO[halfPixel] = surface.HasValidNormal
		? EvaluateGTAO(depthTexture, surface, halfPixel,
			uint2(g_Pass.FullWidth, g_Pass.FullHeight), viewData, g_Pass.Radius,
			g_Pass.FalloffStart, g_Pass.FalloffEnd, g_Pass.Thickness,
			g_Pass.DirectionCount, g_Pass.StepCount)
		: 1.0;
	halfDepth[halfPixel] = surface.ViewZ;
	reconstructedNormal[halfPixel] =
		float4(surface.NormalVS, surface.HasValidNormal ? 1.0 : 0.0);
	selectedOffset[halfPixel] = float2(surface.SelectedOffset);
}
