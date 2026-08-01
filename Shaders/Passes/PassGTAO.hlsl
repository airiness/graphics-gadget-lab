#include <Common/Common.hlsli>
#include <Common/ApplicationBinding.hlsli>
#include <Common/BindlessResources.hlsli>
#include <Lighting/GTAO.hlsli>

#if defined(GGLAB_GTAO_DENOISE_X) || defined(GGLAB_GTAO_DENOISE_Y)

struct GTAODenoisePassParameters
{
	uint SourceAOIndex;
	uint HalfDepthIndex;
	uint OutputAOIndex;
	uint Width;
	uint Height;
	uint Radius;
	uint Padding0;
	uint Padding1;
};

ConstantBuffer<GTAODenoisePassParameters> g_Pass : register(b2);

[numthreads(8, 8, 1)]
void CSMain(uint3 dispatchThreadId : SV_DispatchThreadID)
{
	const uint2 pixel = dispatchThreadId.xy;
	const uint2 extent = uint2(g_Pass.Width, g_Pass.Height);
	if (any(pixel >= extent))
	{
		return;
	}

	Texture2D<float> sourceAO = GetTexture2DFloat(g_Pass.SourceAOIndex);
	Texture2D<float> halfDepth = GetTexture2DFloat(g_Pass.HalfDepthIndex);
	RWTexture2D<float> outputAO = GetRWTexture2DFloat(g_Pass.OutputAOIndex);
#if defined(GGLAB_GTAO_DENOISE_X)
	outputAO[pixel] = DenoiseGTAO(sourceAO, halfDepth, pixel, extent, int2(1, 0), g_Pass.Radius);
#else
	outputAO[pixel] = DenoiseGTAO(sourceAO, halfDepth, pixel, extent, int2(0, 1), g_Pass.Radius);
#endif
}

#elif defined(GGLAB_GTAO_UPSAMPLE)

struct GTAOUpsamplePassParameters
{
	uint DenoisedAOIndex;
	uint HalfDepthIndex;
	uint FullDepthIndex;
	uint FinalAOUavIndex;
	uint ViewIndex;
	uint FullWidth;
	uint FullHeight;
	uint HalfWidth;
	uint HalfHeight;
	uint Padding0;
	uint Padding1;
	uint Padding2;
};

ConstantBuffer<GTAOUpsamplePassParameters> g_Pass : register(b2);

[numthreads(8, 8, 1)]
void CSMain(uint3 dispatchThreadId : SV_DispatchThreadID)
{
	const uint2 fullPixel = dispatchThreadId.xy;
	const uint2 fullExtent = uint2(g_Pass.FullWidth, g_Pass.FullHeight);
	if (any(fullPixel >= fullExtent))
	{
		return;
	}

	Texture2D<float> denoisedAO = GetTexture2DFloat(g_Pass.DenoisedAOIndex);
	Texture2D<float> halfDepth = GetTexture2DFloat(g_Pass.HalfDepthIndex);
	Texture2D<float> fullDepth = GetTexture2DFloat(g_Pass.FullDepthIndex);
	RWTexture2D<float> finalAO = GetRWTexture2DFloat(g_Pass.FinalAOUavIndex);
	const uint viewIndex = g_Scene.ViewBaseIndex + g_Pass.ViewIndex;
	finalAO[fullPixel] = UpsampleGTAO(denoisedAO, halfDepth, fullDepth, fullPixel,
		fullExtent, uint2(g_Pass.HalfWidth, g_Pass.HalfHeight), g_Views[viewIndex]);
}

#else

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
#if defined(GGLAB_GTAO_DIAGNOSTICS)
	RWTexture2D<float4> reconstructedNormal = GetRWTexture2DFloat4(g_Pass.NormalUavIndex);
	RWTexture2D<float2> selectedOffset = GetRWTexture2DFloat2(g_Pass.SelectedOffsetUavIndex);
#endif
	const uint viewIndex = g_Scene.ViewBaseIndex + g_Pass.ViewIndex;
	const ViewData viewData = g_Views[viewIndex];
	const GTAOSurface surface = LoadHalfResolutionSurface(depthTexture, halfPixel,
		uint2(g_Pass.FullWidth, g_Pass.FullHeight), viewData, g_Pass.Radius);

	if (!surface.IsValid)
	{
		rawAO[halfPixel] = 1.0;
		halfDepth[halfPixel] = 0.0;
#if defined(GGLAB_GTAO_DIAGNOSTICS)
		reconstructedNormal[halfPixel] = 0.0.xxxx;
		selectedOffset[halfPixel] = -1.0.xx;
#endif
		return;
	}

	rawAO[halfPixel] = surface.HasValidNormal
		? EvaluateGTAO(depthTexture, surface, halfPixel,
			uint2(g_Pass.FullWidth, g_Pass.FullHeight), viewData, g_Pass.Radius,
			g_Pass.FalloffStart, g_Pass.FalloffEnd, g_Pass.Thickness,
			g_Pass.DirectionCount, g_Pass.StepCount)
		: 1.0;
	halfDepth[halfPixel] = surface.ViewZ;
#if defined(GGLAB_GTAO_DIAGNOSTICS)
	reconstructedNormal[halfPixel] =
		float4(surface.NormalVS, surface.HasValidNormal ? 1.0 : 0.0);
	selectedOffset[halfPixel] = float2(surface.SelectedOffset);
#endif
}

#endif
