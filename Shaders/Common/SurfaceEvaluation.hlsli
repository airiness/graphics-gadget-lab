#pragma once
#include <Common/Common.hlsli>
#include <Common/MaterialUtils.hlsli>

float3 DecodeNormalTexture(float3 sampledRGB, float normalScale)
{
	// glTF RGB normals retain the authored Z when scaling tangent-space XY.
	const float3 normalTS = (sampledRGB * 2.0 - 1.0) * float3(normalScale, normalScale, 1.0);
	return SafeNormalize(normalTS, float3(0.0, 0.0, 1.0));
}

// Keep these values synchronized with MaterialDebugView in MaterialTypes.h.
static const uint MaterialDebugViewLit = 0u;
static const uint MaterialDebugViewBaseColor = 1u;
static const uint MaterialDebugViewMetallic = 2u;
static const uint MaterialDebugViewRoughness = 3u;
static const uint MaterialDebugViewNormal = 4u;
static const uint MaterialDebugViewAuthoredRoughness = 5u;
static const uint MaterialDebugViewEffectiveRoughness = 6u;
static const uint MaterialDebugViewF0 = 7u;
static const uint MaterialDebugViewFeatureFlags = 8u;
static const uint MaterialDebugViewIor = 9u;
static const uint MaterialDebugViewClearcoatFactor = 10u;
static const uint MaterialDebugViewClearcoatRoughness = 11u;
static const uint MaterialDebugViewClearcoatNormal = 12u;
static const uint MaterialDebugViewAnisotropyStrength = 13u;
static const uint MaterialDebugViewAnisotropyDirectionTangent = 14u;
static const uint MaterialDebugViewAnisotropyDirectionWorld = 15u;
static const uint MaterialDebugViewNormalVariance = 19u;
static const uint MaterialDebugViewSpecularAAContribution = 20u;
static const uint MaterialDebugViewEffectiveClearcoatRoughness = 21u;
static const uint MaterialDebugViewAnisotropicAlpha = 22u;
static const uint MaterialDebugViewUnfilteredLit = 23u;

bool NeedsAnisotropyDirection(MaterialData material)
{
	return material.AnisotropyStrength > 0.0 ||
		material.DebugView == MaterialDebugViewAnisotropyDirectionTangent ||
		material.DebugView == MaterialDebugViewAnisotropyDirectionWorld;
}

// Resolves runtime material factors and texture bindings for Forward PBR.
struct SurfaceData
{
	float3 BaseColor;
	float3 Emissive;
	float Metallic;
	float Roughness; // perceived roughness; BRDF clamping stays in the lighting path
	float Ior;
	float ClearcoatFactor;
	float ClearcoatRoughness;
	float AnisotropyStrength;
	float2 AnisotropyDirectionTS;
	float Opacity; // sampled surface alpha; the pass owns alpha mode/cutoff policy
};

// Shading values are derived per pixel and never written back to imported
// material state.
struct BaseShadingState
{
	float3 NormalWS;
	float AuthoredPerceptualRoughness;
	float EffectivePerceptualRoughness;
	float BRDFAlpha;
	float NormalVariance;
	float SpecularAAKernelAlpha;
	float3 F0;
	uint FeatureFlags;
};

struct ClearcoatShadingState
{
	float Factor;
	float3 NormalWS;
	float PerceptualRoughness;
	float BRDFAlpha;
	float NoV;
	float DirectionalAlbedo;
	float3 EnergyCompensation;
};

struct AnisotropyShadingState
{
	float Strength;
	float3 TangentWS;
	float3 BitangentWS;
	float AlphaT;
	float AlphaB;
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
	const float4 metallicRoughnessSampled = SampleMaterialTextureBinding(
		matData.MetallicRoughnessBinding.TextureSamplerBinding, metallicRoughnessUV);
	surface.Metallic = saturate(matData.MetallicFactor * metallicRoughnessSampled.b);
	surface.Roughness = saturate(matData.RoughnessFactor * metallicRoughnessSampled.g);
	surface.Ior = matData.Ior;
	surface.ClearcoatFactor = saturate(matData.ClearcoatFactor);
	if (matData.ClearcoatFactor > 0.0 || matData.DebugView == MaterialDebugViewClearcoatFactor)
	{
		if (matData.ClearcoatBinding.TextureEnabled != 0u)
		{
			const float2 factorUV = SelectUV(matData.ClearcoatBinding, uv0, uv1);
			surface.ClearcoatFactor *= SampleMaterialTextureBinding(
				matData.ClearcoatBinding.TextureSamplerBinding, factorUV).r;
		}
	}
	surface.ClearcoatRoughness = saturate(matData.ClearcoatRoughness);
	// The factor texture can vary within a pixel quad. Do not use its result
	// to guard another implicit-derivative texture sample.
	if ((matData.ClearcoatFactor > 0.0 ||
		(matData.DebugView == MaterialDebugViewClearcoatRoughness ||
			matData.DebugView == MaterialDebugViewEffectiveClearcoatRoughness)) &&
		matData.ClearcoatRoughnessBinding.TextureEnabled != 0u)
	{
		const float2 roughnessUV = SelectUV(matData.ClearcoatRoughnessBinding, uv0, uv1);
		surface.ClearcoatRoughness *= SampleMaterialTextureBinding(
			matData.ClearcoatRoughnessBinding.TextureSamplerBinding, roughnessUV).g;
	}
	surface.AnisotropyStrength = saturate(matData.AnisotropyStrength);
	surface.AnisotropyDirectionTS = float2(1.0, 0.0);
	[branch]
	if (NeedsAnisotropyDirection(matData))
	{
		float2 direction = float2(1.0, 0.0);
		if (matData.AnisotropyBinding.TextureEnabled != 0u)
		{
			const float2 anisotropyUV = SelectUV(matData.AnisotropyBinding, uv0, uv1);
			const float3 anisotropySample = SampleMaterialTextureBinding(
				matData.AnisotropyBinding.TextureSamplerBinding, anisotropyUV).rgb;
			surface.AnisotropyStrength *= anisotropySample.b;
			const float2 sampledDirection = anisotropySample.rg * 2.0 - 1.0;
			if (dot(sampledDirection, sampledDirection) > 1.0e-8)
			{
				direction = normalize(sampledDirection);
			}
		}
		const float cosine = cos(matData.AnisotropyRotation);
		const float sine = sin(matData.AnisotropyRotation);
		surface.AnisotropyDirectionTS = float2(
			cosine * direction.x - sine * direction.y,
			sine * direction.x + cosine * direction.y);
	}

	// Emissive retains the runtime factor scale.
	// A scene-referred emissive-unit migration requires a separate contract.
	const float2 emissiveUV = SelectUV(matData.EmissiveBinding, uv0, uv1);
	surface.Emissive = SampleMaterialTextureBinding(
		matData.EmissiveBinding.TextureSamplerBinding, emissiveUV).rgb *
		matData.EmissiveColorFactor.rgb;

	return surface;
}
