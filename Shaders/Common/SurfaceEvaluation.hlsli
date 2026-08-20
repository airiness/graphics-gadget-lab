#pragma once
#include <Common/MaterialUtils.hlsli>

// gglab.surface profile shape (surface integration probe).
//
// The surface seam answers "what is the material surface" for the existing
// Forward PBR lighting path: it consumes the runtime-driven MaterialData
// (scalar/vector factors plus texture+sampler bindings) and resolves the
// surface quantities the lighting consumes.
//
// Intentionally not owned here (kept in the Forward PBR pass or BRDF):
// normal-map perturbation, BRDF clamps/filters, Forward+ lighting, IBL,
// shadow, GTAO, and post-process behavior.
//
// Sampling policy: the existing logical texture+sampler binding semantics are
// preserved as proven by the probe; no profile-owned sampler policy is
// introduced.

struct SurfaceData
{
	float3 BaseColor;
	float3 Emissive;
	float Metallic;
	float Roughness; // perceived roughness; BRDF clamping stays in the lighting path
	float Opacity;
};

SurfaceData EvaluateSurface(MaterialData matData, float2 uv0, float2 uv1)
{
	SurfaceData surface;

	// BaseColor: sampled base color texture multiplied by the runtime factor.
	const float4 baseColorSampled = SampleMaterialBaseColor(matData, uv0, uv1);
	surface.BaseColor = baseColorSampled.rgb;
	surface.Opacity = ResolveMaterialAlpha(matData, baseColorSampled.a);

	// Metallic/roughness: sampled from the shared texture channel layout
	// (B=metallic, G=roughness) and multiplied by the runtime factors.
	const float2 metallicRoughnessUV = SelectUV(matData.MetallicRoughnessBinding, uv0, uv1);
	const float4 metallicRoughnessSampled = SampleTextureBinding(
		matData.MetallicRoughnessBinding.TextureSamplerBinding, metallicRoughnessUV);
	surface.Metallic = saturate(matData.MetallicFactor * metallicRoughnessSampled.b);
	surface.Roughness = saturate(matData.RoughnessFactor * metallicRoughnessSampled.g);

	// Emissive: sampled emissive texture multiplied by the runtime factor.
	const float2 emissiveUV = SelectUV(matData.EmissiveBinding, uv0, uv1);
	surface.Emissive = SampleTextureBinding(
		matData.EmissiveBinding.TextureSamplerBinding, emissiveUV).rgb *
		matData.EmissiveColorFactor.rgb;

	return surface;
}
