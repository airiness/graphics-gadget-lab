#include <Common/Common.hlsli>
#include <Common/FullscreenTriangle.hlsli>
#include <Common/BindlessResources.hlsli>

struct ComputeLabPassParameters
{
	uint WorkAIndex;
	uint WorkBIndex;
	uint Width;
	uint Height;
	float Phase;
	float PatternFrequency;
	float RingRadius;
	float RingIntensity;
	uint CheckerCellSize;
};

ConstantBuffer<ComputeLabPassParameters> g_Pass : register(b2);

uint PackColor(float4 color)
{
	const uint4 unormColor = uint4(round(saturate(color) * 255.0));
	return unormColor.x |
		(unormColor.y << 8) |
		(unormColor.z << 16) |
		(unormColor.w << 24);
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
	if (dispatchThreadId.x >= g_Pass.Width ||
		dispatchThreadId.y >= g_Pass.Height)
	{
		return;
	}

	RWTexture2D<uint> workA = GetRWTexture2DUint(g_Pass.WorkAIndex);
	RWTexture2D<uint> workB = GetRWTexture2DUint(g_Pass.WorkBIndex);

	const float2 uv =
		(float2(dispatchThreadId.xy) + 0.5) /
		float2(g_Pass.Width, g_Pass.Height);
	const float3 initialized =
		UnpackColor(workA[dispatchThreadId.xy]).rgb;
	const float wave = 0.5 + 0.5 *
		sin(uv.x * g_Pass.PatternFrequency + g_Pass.Phase * 2.0) *
		cos(uv.y * g_Pass.PatternFrequency * 0.78 - g_Pass.Phase);
	const float3 gradient = float3(
		0.08 + 0.75 * uv.x,
		0.12 + 0.68 * uv.y,
		0.25 + 0.55 * wave);
	workA[dispatchThreadId.xy] =
		PackColor(float4(gradient + initialized * 0.35, 1.0));

	const uint2 checkerCoord =
		dispatchThreadId.xy / max(g_Pass.CheckerCellSize, 1);
	const float checker = float((checkerCoord.x ^ checkerCoord.y) & 1);
	const float pulse = 0.5 + 0.5 * sin(g_Pass.Phase * 1.7);
	const float3 checkerColor = lerp(
		float3(0.02, 0.08, 0.15),
		float3(0.18 + 0.35 * pulse, 0.75, 0.92),
		checker);
	workB[dispatchThreadId.xy] =
		PackColor(float4(checkerColor, 1.0));
}

[numthreads(8, 8, 1)]
void CSReadWrite(uint3 dispatchThreadId : SV_DispatchThreadID)
{
	if (dispatchThreadId.x >= g_Pass.Width ||
		dispatchThreadId.y >= g_Pass.Height)
	{
		return;
	}

	RWTexture2D<uint> workA = GetRWTexture2DUint(g_Pass.WorkAIndex);
	const float2 uv =
		(float2(dispatchThreadId.xy) + 0.5) /
		float2(g_Pass.Width, g_Pass.Height);
	const float2 centered = uv * 2.0 - 1.0;
	const float radius = length(centered);
	const float ring = smoothstep(0.055, 0.0,
		abs(radius - (g_Pass.RingRadius + 0.08 * sin(g_Pass.Phase))));
	const float scanline = 0.94 + 0.06 *
		sin(float(dispatchThreadId.y) * 0.35 + g_Pass.Phase * 3.0);

	float4 color = UnpackColor(workA[dispatchThreadId.xy]);
	color.rgb *= scanline;
	color.rgb = lerp(
		color.rgb,
		float3(1.0, 0.28, 0.08),
		ring * g_Pass.RingIntensity);
	workA[dispatchThreadId.xy] = PackColor(color);
}

FullscreenTriangleVSOutput VSMain(uint vertexId : SV_VertexID)
{
	return FullscreenTriangleVS(vertexId);
}

float4 PSMain(FullscreenTriangleVSOutput input) : SV_Target0
{
	Texture2D<uint> workA = GetTexture2DUint(g_Pass.WorkAIndex);
	Texture2D<uint> workB = GetTexture2DUint(g_Pass.WorkBIndex);

	const bool showWorkA = input.UV.x < 0.68;
	const float2 localUv = showWorkA ?
		float2(input.UV.x / 0.68, input.UV.y) :
		float2((input.UV.x - 0.68) / 0.32, input.UV.y);
	const uint2 texel = min(
		uint2(localUv * float2(g_Pass.Width, g_Pass.Height)),
		uint2(g_Pass.Width - 1, g_Pass.Height - 1));
	float3 color = UnpackColor(showWorkA ?
		workA.Load(int3(texel, 0)) :
		workB.Load(int3(texel, 0))).rgb;

	const float divider = 1.0 -
		smoothstep(0.0, 0.0035, abs(input.UV.x - 0.68));
	color = lerp(color, float3(1.0, 1.0, 1.0), divider);
	return float4(saturate(color), 1.0);
}
