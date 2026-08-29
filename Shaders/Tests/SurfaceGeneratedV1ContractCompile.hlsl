// Permanent native-compile gate for the byte-pinned ShaderGraphCore
// profileVersion 1 surface function. The imported file owns only the
// generated EvaluateSurface bytes; this main-repository harness owns the
// real pixel entry and supplies the function's contract inputs.

#include <Tests/Generated/SurfaceGeneratedV1.hlsli>

StructuredBuffer<float> g_SurfaceGeneratedV1ScalarFactors;
StructuredBuffer<float3> g_SurfaceGeneratedV1VectorFactors;

float4 PSMain() : SV_Target
{
	const float metal = g_SurfaceGeneratedV1ScalarFactors[0];
	const float3 tint = g_SurfaceGeneratedV1VectorFactors[0];
	const SurfaceData surface = EvaluateSurface(metal, tint, float2(0.5, 0.5));

	const float checksum = dot(surface.BaseColor, 1.0.xxx) +
		dot(surface.Emissive, 1.0.xxx) +
		surface.Metallic + surface.Roughness + surface.Opacity;

	return float4(surface.BaseColor + float3(checksum, 0.0, 0.0), surface.Opacity);
}
