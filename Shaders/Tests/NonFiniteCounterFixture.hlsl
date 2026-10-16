#include <Common/Common.hlsli>
#include <Common/BufferLayout.hlsli>
#include <Common/TemporalAA.hlsli>

ByteAddressBuffer g_Source : register(t0);
RWByteAddressBuffer g_Output : register(u0);

[numthreads(1, 1, 1)]
void CSMain()
{
	const float4 value = asfloat(g_Source.Load4(0));
	const uint outputSite = NF_SITE_PASS_TEMPORAL_AA_CSMAIN_SANITIZE_HDRCOLOR;
	g_Output.Store4(0, asuint(float4(SanitizeHDRColor(value.xyz, outputSite), SanitizeHDRChannel(value.w, outputSite))));
	float3 history = asfloat(g_Source.Load3(16));
	if (!IsTemporalColorFinite(NonFiniteObserveValue(NF_SITE_PASS_TEMPORAL_AA_CSMAIN_IS_TEMPORAL_COLOR_FINITE, history)))
	{
		history = 0.25.xxx;
	}
	g_Output.Store4(16, asuint(float4(history, 1.0)));
}

float4 VSMain(uint vertex : SV_VertexID) : SV_Position
{
	const float2 uv = float2((vertex << 1) & 2, vertex & 2);
	return float4(uv * float2(2, -2) + float2(-1, 1), 0, 1);
}

float4 PSMain() : SV_Target0
{
	return float4(SanitizeHDRColor(asfloat(g_Source.Load3(0)), NF_SITE_PASS_TEMPORAL_AA_CSMAIN_SANITIZE_HDRCOLOR), 1.0);
}
