#include <Common/SurfaceEvaluation.hlsli>
#include <PBR/SpecularAA.hlsli>

// Keep each runtime surface field observable in a production shader compile.
StructuredBuffer<MaterialData> g_SurfaceContractMaterials;

float4 PSMain() : SV_Target
{
	const MaterialData matData = g_SurfaceContractMaterials[0];
	const SurfaceData surface = EvaluateSurface(matData, float2(0.5, 0.5), float2(0.5, 0.5));
	const SpecularAAResult specularAA = EvaluateSpecularAA(surface.Roughness,
		float3(surface.BaseColor.xy, 1.0), true);
	const float2 anisotropicAlpha = FilterAnisotropicAlpha(
		PerceptualRoughnessToAlpha(ClampPerceptualRoughnessForBRDF(surface.Roughness)),
		surface.AnisotropyStrength, specularAA.KernelAlpha);

	// Keep the profile fields observable so this contract cannot be dropped
	// wholesale by dead-code elimination.
	const float checksum = dot(surface.BaseColor, 1.0.xxx) + dot(surface.Emissive, 1.0.xxx) +
		surface.Metallic + surface.Roughness + surface.Ior + surface.Opacity +
		surface.ClearcoatFactor + surface.ClearcoatRoughness +
		surface.AnisotropyStrength + dot(surface.AnisotropyDirectionTS, 1.0.xx) +
		matData.NormalScale +
		matData.OcclusionStrength + matData.AlphaCutoff + matData.AlphaMode +
		matData.Flags + matData.DebugView + specularAA.NormalVariance +
		specularAA.EffectivePerceptualRoughness + dot(anisotropicAlpha, 1.0.xx) +
		matData.ClearcoatNormalScale + matData.AnisotropyRotation +
		matData.ClearcoatBinding.TextureEnabled +
		matData.ClearcoatRoughnessBinding.TextureEnabled +
		matData.ClearcoatNormalBinding.TextureEnabled +
		matData.AnisotropyTextureEnabled +
		dot(matData.NormalBinding.UVTransformU, 1.0.xxxx) +
		dot(matData.OcclusionBinding.UVTransformV, 1.0.xxxx);

	return float4(surface.BaseColor + float3(checksum, 0.0, 0.0), surface.Opacity);
}
