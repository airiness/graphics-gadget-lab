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
		matData.AnisotropyBinding.TextureEnabled + dot(matData.AnisotropyPadding, uint2(1u, 1u)) +
		dot(matData.NormalBinding.UVTransformU, 1.0.xxxx) +
		dot(matData.OcclusionBinding.UVTransformV, 1.0.xxxx);

	return float4(surface.BaseColor + float3(checksum, 0.0, 0.0), surface.Opacity);
}

// Constant inputs exercise the production decoder without a GPU or a CPU copy.
// The optimized DXIL must fold every component to zero on success.
float4 NormalDecodeContractPS() : SV_Target
{
	const float3 sampledRGB = float3(0.8, 0.3, 0.75);
	const bool matches =
		all(abs(DecodeNormalTexture(sampledRGB, 0.0) - float3(0.0, 0.0, 1.0)) < 1.0e-5) &&
		all(abs(DecodeNormalTexture(sampledRGB, 0.5) -
			float3(0.4866642634, -0.3244428423, 0.8111071057)) < 1.0e-5) &&
		all(abs(DecodeNormalTexture(sampledRGB, 1.0) -
			float3(0.6837634588, -0.4558423058, 0.5698028823)) < 1.0e-5) &&
		all(abs(DecodeNormalTexture(sampledRGB, 2.0) -
			float3(0.7861461385, -0.5240974257, 0.3275608911)) < 1.0e-5) &&
		all(abs(DecodeNormalTexture(sampledRGB, -1.0) -
			float3(-0.6837634588, 0.4558423058, 0.5698028823)) < 1.0e-5) &&
		all(abs(DecodeNormalTexture(float3(0.5, 0.5, 1.0), 2.0) - float3(0.0, 0.0, 1.0)) < 1.0e-5) &&
		all(abs(DecodeNormalTexture(0.5.xxx, 1.0) - float3(0.0, 0.0, 1.0)) < 1.0e-5);
	return matches ? 0.0.xxxx : 1.0.xxxx;
}
