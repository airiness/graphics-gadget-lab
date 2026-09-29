#include <Common/SurfaceEvaluation.hlsli>

// Keep each runtime surface field observable in a production shader compile.
StructuredBuffer<MaterialData> g_SurfaceContractMaterials;

float4 PSMain() : SV_Target
{
	const MaterialData matData = g_SurfaceContractMaterials[0];
	const SurfaceData surface = EvaluateSurface(matData, float2(0.5, 0.5), float2(0.5, 0.5));

	// Keep the profile fields observable so this contract cannot be dropped
	// wholesale by dead-code elimination.
	const float checksum = dot(surface.BaseColor, 1.0.xxx) + dot(surface.Emissive, 1.0.xxx) +
		surface.Metallic + surface.Roughness + surface.Ior + surface.Opacity + matData.NormalScale +
		matData.OcclusionStrength + matData.AlphaCutoff + matData.AlphaMode +
		matData.Flags + matData.DebugView +
		dot(matData.NormalBinding.UVTransformU, 1.0.xxxx) +
		dot(matData.OcclusionBinding.UVTransformV, 1.0.xxxx);

	return float4(surface.BaseColor + float3(checksum, 0.0, 0.0), surface.Opacity);
}
