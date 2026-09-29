#pragma once
#include <Common/MaterialUtils.hlsli>

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
static const uint MaterialDebugViewSheenColor = 16u;
static const uint MaterialDebugViewSheenRoughness = 17u;
static const uint MaterialDebugViewSheenContribution = 18u;

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
	float3 SheenColor;
	float SheenRoughness;
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

struct SheenShadingState
{
	float3 Color;
	float PerceptualRoughness;
	float Alpha;
	float ViewAlbedoBound;
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
	surface.ClearcoatFactor = saturate(matData.ClearcoatFactor);
	if (matData.ClearcoatFactor > 0.0 || matData.DebugView == MaterialDebugViewClearcoatFactor)
	{
		if (matData.ClearcoatBinding.TextureEnabled != 0u)
		{
			const float2 factorUV = SelectUV(matData.ClearcoatBinding, uv0, uv1);
			surface.ClearcoatFactor *= SampleTextureBinding(
				matData.ClearcoatBinding.TextureSamplerBinding, factorUV).r;
		}
	}
	surface.ClearcoatRoughness = saturate(matData.ClearcoatRoughness);
	// The factor texture can vary within a pixel quad. Do not use its result
	// to guard another implicit-derivative texture sample.
	if ((matData.ClearcoatFactor > 0.0 ||
		matData.DebugView == MaterialDebugViewClearcoatRoughness) &&
		matData.ClearcoatRoughnessBinding.TextureEnabled != 0u)
	{
		const float2 roughnessUV = SelectUV(matData.ClearcoatRoughnessBinding, uv0, uv1);
		surface.ClearcoatRoughness *= SampleTextureBinding(
			matData.ClearcoatRoughnessBinding.TextureSamplerBinding, roughnessUV).g;
	}
	surface.AnisotropyStrength = saturate(matData.AnisotropyStrength);
	float2 direction = float2(1.0, 0.0);
	if ((matData.AnisotropyStrength > 0.0 ||
		matData.DebugView == MaterialDebugViewAnisotropyDirectionTangent ||
		matData.DebugView == MaterialDebugViewAnisotropyDirectionWorld) &&
		matData.AnisotropyTextureEnabled != 0u)
	{
		const float2 anisotropyUV = SelectUV(matData.AnisotropyBinding, uv0, uv1);
		const float3 anisotropySample = SampleTextureBinding(
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
	surface.SheenColor = saturate(matData.SheenColorFactor.rgb);
	surface.SheenRoughness = saturate(matData.SheenRoughnessFactor);
	if (any(matData.SheenColorFactor.rgb > 0.0.xxx) ||
		matData.DebugView == MaterialDebugViewSheenRoughness)
	{
		if (any(matData.SheenColorFactor.rgb > 0.0.xxx))
		{
			const float2 colorUV = SelectUV(matData.SheenColorBinding, uv0, uv1);
			surface.SheenColor *= SampleTextureBinding(
				matData.SheenColorBinding.TextureSamplerBinding, colorUV).rgb;
		}
		if (any(matData.SheenColorFactor.rgb > 0.0.xxx) ||
			matData.DebugView == MaterialDebugViewSheenRoughness)
		{
			const float2 roughnessUV = SelectUV(matData.SheenRoughnessBinding, uv0, uv1);
			surface.SheenRoughness *= SampleTextureBinding(
				matData.SheenRoughnessBinding.TextureSamplerBinding, roughnessUV).a;
		}
	}

	// Emissive retains the runtime factor scale.
	// A scene-referred emissive-unit migration requires a separate contract.
	const float2 emissiveUV = SelectUV(matData.EmissiveBinding, uv0, uv1);
	surface.Emissive = SampleTextureBinding(
		matData.EmissiveBinding.TextureSamplerBinding, emissiveUV).rgb *
		matData.EmissiveColorFactor.rgb;

	return surface;
}
