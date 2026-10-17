#include <Common/Common.hlsli>
#include <Common/FullscreenTriangle.hlsli>
#include <Common/BindlessResources.hlsli>

struct ComputeLabPassParameters
{
	uint WorkAIndex;
	uint WorkBIndex;
	uint Width;
	uint Height;
	uint Padding0;
	float PatternFrequency;
	float RingRadius;
	float RingIntensity;
	uint CheckerCellSize;
	uint ProbeIndex;
	uint2 Padding1;
	float4 CheckerDarkColor;
	float4 CheckerLightColor;
	float4 RingColor;
};

ConstantBuffer<ComputeLabPassParameters> g_Pass : register(b2);

struct ComputeLabInlineParameters
{
	float Phase;
	float PreviewSplit;
};

ConstantBuffer<ComputeLabInlineParameters> g_Inline : register(b4);

uint PackColor(float4 color)
{
	const uint4 unormColor = (uint4)(round(saturate(color) * 255.0));
	return unormColor.x | (unormColor.y << 8) | (unormColor.z << 16) | (unormColor.w << 24);
}

float4 UnpackColor(uint packedColor)
{
	return float4(
		packedColor & 0xff,
		(packedColor >> 8) & 0xff,
		(packedColor >> 16) & 0xff,
		(packedColor >> 24) & 0xff) / 255.0;
}

[numthreads(8, 8, 1)]
void CSWrite(uint3 dispatchThreadId : SV_DispatchThreadID)
{
	if (dispatchThreadId.x >= g_Pass.Width || dispatchThreadId.y >= g_Pass.Height)
	{
		return;
	}

	RWTexture2D<uint> workA = GetRWTexture2DUint(g_Pass.WorkAIndex);
	RWTexture2D<uint> workB = GetRWTexture2DUint(g_Pass.WorkBIndex);

	const float2 uv = ((float2)(dispatchThreadId.xy) + 0.5) / float2(g_Pass.Width, g_Pass.Height);
	const float3 initialized = UnpackColor(workA[dispatchThreadId.xy]).rgb;
	const float wave = 0.5 + 0.5 * sin(uv.x * g_Pass.PatternFrequency + g_Inline.Phase * 2.0) *
		cos(uv.y * g_Pass.PatternFrequency * 0.78 - g_Inline.Phase);
	const float3 gradient = float3(0.08 + 0.75 * uv.x, 0.12 + 0.68 * uv.y, 0.25 + 0.55 * wave);
	workA[dispatchThreadId.xy] = PackColor(float4(gradient + initialized * 0.35, 1.0));

	const uint2 checkerCoord = dispatchThreadId.xy / max(g_Pass.CheckerCellSize, 1);
	const float checker = (float)((checkerCoord.x ^ checkerCoord.y) & 1);
	const float pulse = 0.5 + 0.5 * sin(g_Inline.Phase * 1.7);
	const float3 checkerColor =
		lerp(g_Pass.CheckerDarkColor.rgb, g_Pass.CheckerLightColor.rgb + float3(0.35 * pulse, 0.0, 0.0), checker);
	workB[dispatchThreadId.xy] = PackColor(float4(checkerColor, 1.0));
}

[numthreads(8, 8, 1)]
void CSReadWrite(uint3 dispatchThreadId : SV_DispatchThreadID)
{
	if (dispatchThreadId.x >= g_Pass.Width || dispatchThreadId.y >= g_Pass.Height)
	{
		return;
	}

	RWTexture2D<uint> workA = GetRWTexture2DUint(g_Pass.WorkAIndex);
	const float2 uv = ((float2)(dispatchThreadId.xy) + 0.5) / float2(g_Pass.Width, g_Pass.Height);
	const float2 centered = uv * 2.0 - 1.0;
	const float radius = length(centered);
	const float ring =
		smoothstep(0.055, 0.0, abs(radius - (g_Pass.RingRadius + 0.08 * sin(g_Inline.Phase))));
	const float scanline = 0.94 + 0.06 * sin((float)(dispatchThreadId.y) * 0.35 + g_Inline.Phase * 3.0);

	float4 color = UnpackColor(workA[dispatchThreadId.xy]);
	color.rgb *= scanline;
	color.rgb = lerp(color.rgb, g_Pass.RingColor.rgb, ring * g_Pass.RingIntensity);
	workA[dispatchThreadId.xy] = PackColor(color);
}

// Writes finite probe values. A watch injection at this point replaces one texel's
// component, so a boundary scan of the probe must report exactly that location.
[numthreads(8, 8, 1)]
void CSNonFiniteProbe(uint3 dispatchThreadId : SV_DispatchThreadID)
{
	if (dispatchThreadId.x >= g_Pass.Width || dispatchThreadId.y >= g_Pass.Height)
	{
		return;
	}

	RWTexture2D<float4> probe = GetRWTexture2DFloat4(g_Pass.ProbeIndex);
	const float2 uv = ((float2)(dispatchThreadId.xy) + 0.5) / float2(g_Pass.Width, g_Pass.Height);
	probe[dispatchThreadId.xy] = NonFiniteInject(
		NF_INJECT_PASS_RENDER_GRAPH_COMPUTE_SMOKE_CSNONFINITEPROBE_PROBE, dispatchThreadId.xy,
		float4(uv, 0.5, 1.0));
}

FullscreenTriangleVSOutput VSMain(uint vertexId : SV_VertexID)
{
	return FullscreenTriangleVS(vertexId);
}

float4 PSMain(FullscreenTriangleVSOutput input) : SV_Target0
{
	Texture2D<uint> workA = GetTexture2DUint(g_Pass.WorkAIndex);
	Texture2D<uint> workB = GetTexture2DUint(g_Pass.WorkBIndex);

	const bool showWorkA = input.UV.x < g_Inline.PreviewSplit;
	const float2 localUv = showWorkA ?
		float2(input.UV.x / g_Inline.PreviewSplit, input.UV.y) :
		float2((input.UV.x - g_Inline.PreviewSplit) / (1.0 - g_Inline.PreviewSplit), input.UV.y);
	const uint2 texel = min((uint2)(localUv * float2(g_Pass.Width, g_Pass.Height)),
		uint2(g_Pass.Width - 1, g_Pass.Height - 1));
	float3 color =
		UnpackColor(showWorkA ? workA.Load(int3(texel, 0)) : workB.Load(int3(texel, 0))).rgb;

	const float divider = 1.0 - smoothstep(0.0, 0.0035, abs(input.UV.x - g_Inline.PreviewSplit));
	color = lerp(color, float3(1.0, 1.0, 1.0), divider);
	float4 output = float4(color, 1.0);
#if defined(GGLAB_NONFINITE_COUNTERS)
	// A watch injection here exists only in registers at one display pixel. The
	// sanitizing write replaces non-finite and negative values with zero, so only the
	// production counter of this site can observe them; other components add zero.
	const float4 injected = NonFiniteInject(NF_INJECT_PASS_RENDER_GRAPH_COMPUTE_SMOKE_PSMAIN_PREVIEW,
		(uint2)(input.PositionCS.xy), float4(0.0, 0.0, 0.0, 0.0));
	[unroll]
	for (uint channel = 0; channel < 4u; ++channel)
	{
		output[channel] += SanitizeHDRChannel(
			injected[channel], NF_SITE_PASS_RENDER_GRAPH_COMPUTE_SMOKE_PSMAIN_SANITIZE_HDRCHANNEL);
	}
#endif
	return saturate(output);
}
