#include <Common/ApplicationBinding.hlsli>
#include <Common/BindlessResources.hlsli>

// Evaluation-only supersampled reference: adds the current HDR scene color to the
// running RGBA32F sum and writes the running mean that post-processing receives.
struct TemporalReferencePassParameters
{
	uint CurrentColorIndex;
	uint PreviousSumIndex;
	uint NextSumUavIndex;
	uint MeanColorUavIndex;
	// Zero starts a new sum; the previous sum is then not read.
	uint SampleIndex;
};

ConstantBuffer<TemporalReferencePassParameters> g_Pass : register(b2);

[numthreads(8, 8, 1)]
void CSMain(uint3 dispatchThreadId : SV_DispatchThreadID)
{
	Texture2D<float4> currentColorTexture = GetTexture2DFloat4(g_Pass.CurrentColorIndex);
	uint width;
	uint height;
	currentColorTexture.GetDimensions(width, height);
	const uint2 pixel = dispatchThreadId.xy;
	if (any(pixel >= uint2(width, height)))
	{
		return;
	}

	float3 currentColor = currentColorTexture.Load(int3(pixel, 0)).rgb;
	// A non-finite sample would poison every later mean; it contributes black instead.
	if (!all(isfinite(currentColor)))
	{
		currentColor = 0.0.xxx;
	}
	float3 sum = currentColor;
	if (g_Pass.SampleIndex > 0)
	{
		sum += GetTexture2DFloat4(g_Pass.PreviousSumIndex).Load(int3(pixel, 0)).rgb;
	}

	RWTexture2D<float4> nextSum = GetRWTexture2DFloat4(g_Pass.NextSumUavIndex);
	RWTexture2D<float4> meanColor = GetRWTexture2DFloat4(g_Pass.MeanColorUavIndex);
	nextSum[pixel] = float4(sum, 1.0);
	meanColor[pixel] = float4(sum / float(g_Pass.SampleIndex + 1), 1.0);
}
