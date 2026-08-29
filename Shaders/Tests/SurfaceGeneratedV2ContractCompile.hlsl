// Permanent native-compile gate for the byte-pinned ShaderGraphCore
// profileVersion 2 surface function. The imported file owns the generated
// uint2 texture/sampler signature and bindless sample form; this
// main-repository harness owns the real pixel entry and draw-time inputs.

#include <Tests/Generated/SurfaceGeneratedV2.hlsli>

StructuredBuffer<float> g_SurfaceGeneratedV2ScalarFactors;
StructuredBuffer<uint2> g_SurfaceGeneratedV2TextureBindings;

float4 PSMain() : SV_Target
{
	const float roughnessFactor = g_SurfaceGeneratedV2ScalarFactors[0];
	const uint2 textureSamplerBinding = g_SurfaceGeneratedV2TextureBindings[0];
	const float2 uv0 = float2(0.5, 0.5);

	const SurfaceData surface = EvaluateSurface(
		roughnessFactor, textureSamplerBinding, uv0);

	const float checksum = dot(surface.BaseColor, 1.0.xxx) +
		dot(surface.Emissive, 1.0.xxx) +
		surface.Metallic + surface.Roughness + surface.Opacity;

	return float4(surface.BaseColor + float3(checksum, 0.0, 0.0), surface.Opacity);
}
