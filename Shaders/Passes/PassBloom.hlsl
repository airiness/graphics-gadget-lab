#include <Common/Common.hlsli>
#include <Common/FullscreenTriangle.hlsli>
#include <Common/MaterialSampling.hlsli>

#define BLOOM_FILTER_MODE_PREFILTER 0
#define BLOOM_FILTER_MODE_DOWNSAMPLE 1
#define BLOOM_FILTER_MODE_UPSAMPLE 2

struct BloomPassParameters
{
	uint SourceTextureIndex;
	uint SourceSamplerIndex;
	uint FilterMode;
	uint Padding0;
	float2 SourceTexelSize;
	float Threshold;
	float SoftKnee;
	float ExposureScaleOverPreExposure;
	float Scatter;
	float2 Padding1;
};

ConstantBuffer<BloomPassParameters> g_Pass : register(b2);

FullscreenTriangleVSOutput VSMain(uint vertexId : SV_VertexID)
{
	return FullscreenTriangleVS(vertexId);
}

float3 SampleSource(float2 uv)
{
	return SanitizeHDRColor(
		SampleTexture2DLevel(g_Pass.SourceTextureIndex, g_Pass.SourceSamplerIndex, uv, 0.0).rgb);
}

float3 Downsample(float2 uv)
{
	const float2 texel = g_Pass.SourceTexelSize;
	float3 result = SampleSource(uv) * 0.25;
	result += SampleSource(uv + float2(-texel.x, 0.0)) * 0.125;
	result += SampleSource(uv + float2(texel.x, 0.0)) * 0.125;
	result += SampleSource(uv + float2(0.0, -texel.y)) * 0.125;
	result += SampleSource(uv + float2(0.0, texel.y)) * 0.125;
	result += SampleSource(uv + float2(-texel.x, -texel.y)) * 0.0625;
	result += SampleSource(uv + float2(texel.x, -texel.y)) * 0.0625;
	result += SampleSource(uv + float2(-texel.x, texel.y)) * 0.0625;
	result += SampleSource(uv + float2(texel.x, texel.y)) * 0.0625;
	return result;
}

float3 Prefilter(float3 storedColor)
{
	const float3 exposedColor = storedColor * g_Pass.ExposureScaleOverPreExposure;
	const float brightness = max(exposedColor.r, max(exposedColor.g, exposedColor.b));
	const float knee = g_Pass.Threshold * g_Pass.SoftKnee;
	float soft = brightness - g_Pass.Threshold + knee;
	soft = clamp(soft, 0.0, 2.0 * knee);
	soft = soft * soft / max(4.0 * knee, 1e-5);
	const float contribution = max(soft, brightness - g_Pass.Threshold) / max(brightness, 1e-5);
	return storedColor * saturate(contribution);
}

float3 Upsample(float2 uv)
{
	const float2 texel = g_Pass.SourceTexelSize;
	float3 result = SampleSource(uv) * 4.0;
	result += SampleSource(uv + float2(-texel.x, 0.0)) * 2.0;
	result += SampleSource(uv + float2(texel.x, 0.0)) * 2.0;
	result += SampleSource(uv + float2(0.0, -texel.y)) * 2.0;
	result += SampleSource(uv + float2(0.0, texel.y)) * 2.0;
	result += SampleSource(uv + float2(-texel.x, -texel.y));
	result += SampleSource(uv + float2(texel.x, -texel.y));
	result += SampleSource(uv + float2(-texel.x, texel.y));
	result += SampleSource(uv + float2(texel.x, texel.y));
	return result * (g_Pass.Scatter / 16.0);
}

float4 PSMain(FullscreenTriangleVSOutput input) : SV_Target0
{
	float3 color = 0.0;
	if (g_Pass.FilterMode == BLOOM_FILTER_MODE_UPSAMPLE)
	{
		color = Upsample(input.UV);
	}
	else if (g_Pass.FilterMode == BLOOM_FILTER_MODE_PREFILTER)
	{
		color = Prefilter(Downsample(input.UV));
	}
	else if (g_Pass.FilterMode == BLOOM_FILTER_MODE_DOWNSAMPLE)
	{
		color = Downsample(input.UV);
	}
	return float4(SanitizeHDRColor(color), 0.0);
}
