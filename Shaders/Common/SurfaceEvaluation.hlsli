#pragma once
#include <Common/MaterialUtils.hlsli>

// Resolves runtime material factors and texture bindings for Forward PBR.
struct SurfaceData
{
	float3 BaseColor;
	float3 Emissive;
	float Metallic;
	float Roughness; // perceived roughness; BRDF clamping stays in the lighting path
	float Ior;
	float Opacity; // sampled surface alpha; the pass owns alpha mode/cutoff policy
};

// Shading values are derived per pixel and never written back to imported
// material state. Optional lobes can extend this seam in later slices.
struct BaseShadingState
{
	float3 NormalWS;
	float AuthoredPerceptualRoughness;
	float EffectivePerceptualRoughness;
	float BRDFAlpha;
	float3 F0;
	uint FeatureFlags;
};

SurfaceData EvaluateSurface(MaterialData matData, float2 uv0, float2 uv1)
{
	SurfaceData surface;

	// BaseColor: sampled base color texture multiplied by the runtime factor.
	const float4 baseColorSampled = SampleMaterialBaseColor(matData, uv0, uv1);
	surface.BaseColor = baseColorSampled.rgb;
	// Opacity stays the raw sampled alpha: alpha mode / cutoff / discard policy
	// belongs to the pass, not to the surface quantities.
	surface.Opacity = baseColorSampled.a;

	// Metallic/roughness: sampled from the shared texture channel layout
	// (B=metallic, G=roughness) and multiplied by the runtime factors.
	const float2 metallicRoughnessUV = SelectUV(matData.MetallicRoughnessBinding, uv0, uv1);
	const float4 metallicRoughnessSampled = SampleTextureBinding(
		matData.MetallicRoughnessBinding.TextureSamplerBinding, metallicRoughnessUV);
	surface.Metallic = saturate(matData.MetallicFactor * metallicRoughnessSampled.b);
	surface.Roughness = saturate(matData.RoughnessFactor * metallicRoughnessSampled.g);
	surface.Ior = matData.Ior;

	// Emissive retains the pre-World-Lighting legacy factor scale in this baseline.
	// A scene-referred emissive-unit migration requires a separate contract.
	const float2 emissiveUV = SelectUV(matData.EmissiveBinding, uv0, uv1);
	surface.Emissive = SampleTextureBinding(
		matData.EmissiveBinding.TextureSamplerBinding, emissiveUV).rgb *
		matData.EmissiveColorFactor.rgb;

	return surface;
}
