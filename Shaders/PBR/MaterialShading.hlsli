#pragma once
#include <Common/SurfaceEvaluation.hlsli>
#include <PBR/SpecularAA.hlsli>

// Per-pixel inputs and derived state, not GPU upload contracts.
struct MaterialShadingInput
{
	float3 PositionWS;
	float3 NormalWS;
	float4 TangentWS;
	float2 UV0;
	float2 UV1;
	float3 ViewPositionWS;
	bool IsFrontFace;
};

struct MaterialShadingFrame
{
	BaseShadingState Base;
	SpecularAAResult ClearcoatSpecularAA;
	float3 ClearcoatNormalWS;
	AnisotropyShadingState Anisotropy;
	float3 ViewDirectionWS;
	float NoV;
	float ClearcoatNoV;
};

struct PreparedMaterialShading
{
	BaseShadingState Base;
	ClearcoatShadingState Clearcoat;
	AnisotropyShadingState Anisotropy;
	float3 ViewDirectionWS;
	float NoV;
	float3 BaseColor;
	float3 EnergyCompensation;
	float3 SpecularDirectionalAlbedo;
	float3 DiffuseWeight;
};

bool NeedsClearcoatSpecularAA(MaterialData material)
{
	// Use draw-uniform inputs, never a factor sampled within the pixel quad.
	return material.ClearcoatFactor > 0.0 ||
		material.DebugView == MaterialDebugViewEffectiveClearcoatRoughness;
}

SpecularAAResult PrepareClearcoatSpecularAA(MaterialData material, SurfaceData surface,
	float3 normalWS)
{
	[branch]
	if (NeedsClearcoatSpecularAA(material))
	{
		return EvaluateSpecularAA(surface.ClearcoatRoughness, normalWS,
			material.DebugView != MaterialDebugViewUnfilteredLit);
	}
	SpecularAAResult result;
	result.NormalVariance = 0.0;
	result.KernelAlpha = 0.0;
	result.EffectivePerceptualRoughness = ClampPerceptualRoughnessForBRDF(surface.ClearcoatRoughness);
	return result;
}

bool RequiresDerivedNormalFrame(MaterialTextureBindingData binding)
{
	return binding.TexCoordIndex != 0u ||
		any(abs(binding.UVTransformU.xy - float2(1.0, 0.0)) > 1.0e-6) ||
		any(abs(binding.UVTransformV.xy - float2(0.0, 1.0)) > 1.0e-6);
}

float3x3 BuildNormalTextureFrame(MaterialTextureBindingData binding,
	float3 normalWS, float4 tangentWS, float3 positionWS, float2 uv)
{
	// Imported generated tangents use UV0. Normal mapping and anisotropy must
	// share the frame derived from the actual normal coordinates when these differ.
	return RequiresDerivedNormalFrame(binding)
		? BuildTBN(normalWS, positionWS, uv)
		: BuildTBNFromTangent(normalWS, tangentWS, positionWS, uv);
}

float3 SampleNormalWS(MaterialTextureBindingData binding, float normalScale, float3x3 TBN, float2 uv)
{
	const float3 normalSampled = DecodeNormalTexture(
		SampleTextureBinding(binding.TextureSamplerBinding, uv).rgb, normalScale);
	return SafeNormalize(mul(normalSampled, TBN), TBN[2]);
}

BaseShadingState BuildBaseShadingState(SurfaceData surface, float3 normalWS,
	bool specularAAEnabled)
{
	BaseShadingState state;
	state.NormalWS = normalWS;
	state.AuthoredPerceptualRoughness = surface.Roughness;
	const SpecularAAResult specularAA = EvaluateSpecularAA(surface.Roughness, normalWS,
		specularAAEnabled);
	state.EffectivePerceptualRoughness = specularAA.EffectivePerceptualRoughness;
	state.BRDFAlpha = PerceptualRoughnessToAlpha(state.EffectivePerceptualRoughness);
	state.NormalVariance = specularAA.NormalVariance;
	state.SpecularAAKernelAlpha = specularAA.KernelAlpha;
	// glTF reserves zero for an infinite-IOR Fresnel response (F0 = 1).
	const float ior = surface.Ior == 0.0 ? 0.0 : max(surface.Ior, 1.0);
	const float dielectricReflectance = (ior - 1.0) / (ior + 1.0);
	state.F0 = lerp((dielectricReflectance * dielectricReflectance).xxx,
		surface.BaseColor, surface.Metallic);
	state.FeatureFlags = 0u;
	if (surface.ClearcoatFactor > 0.0)
	{
		state.FeatureFlags |= 1u;
	}
	if (surface.AnisotropyStrength > 0.0)
	{
		state.FeatureFlags |= 2u;
	}
	return state;
}

AnisotropyShadingState BuildAnisotropyShadingState(SurfaceData surface, float3 normalWS,
	float3 shadingNormalWS, float3x3 frame,
	float authoredBaseAlpha, float kernelAlpha, bool evaluateFrame)
{
	AnisotropyShadingState state;
	state.Strength = surface.AnisotropyStrength;
	state.TangentWS = 0.0.xxx;
	state.BitangentWS = 0.0.xxx;
	state.AlphaB = saturate(authoredBaseAlpha + kernelAlpha);
	state.AlphaT = state.AlphaB;
	if (!evaluateFrame) return state;
	const float2 filteredAlpha = FilterAnisotropicAlpha(authoredBaseAlpha,
		state.Strength, kernelAlpha);
	state.AlphaT = filteredAlpha.x;
	state.AlphaB = filteredAlpha.y;

	const float2 direction = surface.AnisotropyDirectionTS;
	const float3 tangent = frame[0] * direction.x + frame[1] * direction.y;
	const float3 projected = tangent - shadingNormalWS * dot(shadingNormalWS, tangent);
	const float3 fallbackTangent = SafeNormalize(cross(frame[1], shadingNormalWS), frame[0]);
	state.TangentWS = SafeNormalize(projected, fallbackTangent);
	const float handedness = dot(cross(frame[0], frame[1]), normalWS) < 0.0 ? -1.0 : 1.0;
	state.BitangentWS = SafeNormalize(cross(shadingNormalWS, state.TangentWS), frame[1]) * handedness;
	return state;
}

MaterialShadingFrame PrepareMaterialShadingFrame(MaterialData material, SurfaceData surface,
	MaterialShadingInput input)
{
	float3 normalWS = SafeNormalize(input.NormalWS, float3(0.0, 1.0, 0.0));
	if ((material.Flags & 1u) != 0u && !input.IsFrontFace)
	{
		normalWS = -normalWS;
	}
	const float2 normalUV = SelectUV(material.NormalBinding, input.UV0, input.UV1);
	const float3x3 normalFrame = BuildNormalTextureFrame(material.NormalBinding,
		normalWS, input.TangentWS, input.PositionWS, normalUV);
	const float3 baseNormalWS = SampleNormalWS(material.NormalBinding, material.NormalScale,
		normalFrame, normalUV);
	MaterialShadingFrame frame;
	frame.ClearcoatNormalWS = normalWS;
	// Material factors and debug selection are uniform within a draw. The
	// sampled coat factor must not control an implicit-derivative normal sample.
	if ((material.ClearcoatFactor > 0.0 ||
		(material.DebugView == MaterialDebugViewClearcoatNormal ||
			material.DebugView == MaterialDebugViewEffectiveClearcoatRoughness)) &&
		material.ClearcoatNormalBinding.TextureEnabled != 0u)
	{
		const float2 clearcoatUV = SelectUV(material.ClearcoatNormalBinding, input.UV0, input.UV1);
		const float3x3 clearcoatFrame = BuildNormalTextureFrame(material.ClearcoatNormalBinding,
			normalWS, input.TangentWS, input.PositionWS, clearcoatUV);
		frame.ClearcoatNormalWS = SampleNormalWS(material.ClearcoatNormalBinding,
			material.ClearcoatNormalScale, clearcoatFrame, clearcoatUV);
	}
	const bool specularAAEnabled = material.DebugView != MaterialDebugViewUnfilteredLit;
	frame.Base = BuildBaseShadingState(surface, baseNormalWS, specularAAEnabled);
	// Evaluate required normal footprints ahead of sampled feature and diagnostic branches.
	frame.ClearcoatSpecularAA = PrepareClearcoatSpecularAA(material, surface, frame.ClearcoatNormalWS);
	frame.Anisotropy = BuildAnisotropyShadingState(surface, normalWS, baseNormalWS,
		normalFrame,
		PerceptualRoughnessToAlpha(ClampPerceptualRoughnessForBRDF(surface.Roughness)),
		frame.Base.SpecularAAKernelAlpha,
		material.AnisotropyStrength > 0.0 ||
		material.DebugView == MaterialDebugViewAnisotropyDirectionWorld);
	frame.ViewDirectionWS = SafeNormalize(input.ViewPositionWS - input.PositionWS, baseNormalWS);
	frame.NoV = saturate(dot(baseNormalWS, frame.ViewDirectionWS));
	frame.ClearcoatNoV = saturate(dot(frame.ClearcoatNormalWS, frame.ViewDirectionWS));
	return frame;
}

ClearcoatShadingState BuildClearcoatShadingState(SurfaceData surface, MaterialShadingFrame frame)
{
	ClearcoatShadingState coat;
	coat.Factor = surface.ClearcoatFactor;
	coat.NormalWS = frame.ClearcoatNormalWS;
	coat.DirectionalAlbedo = 0.0;
	coat.EnergyCompensation = 1.0.xxx;
	coat.PerceptualRoughness = frame.ClearcoatSpecularAA.EffectivePerceptualRoughness;
	coat.BRDFAlpha = PerceptualRoughnessToAlpha(coat.PerceptualRoughness);
	coat.NoV = frame.ClearcoatNoV;
	return coat;
}

void ApplyClearcoatDirectionalEnergy(inout ClearcoatShadingState coat, float2 brdfLUT)
{
	// The caller keeps this together with the active coat's LUT fetch, so the
	// resource access and energy preparation need only one feature branch.
	coat.EnergyCompensation = GGXEnergyCompensation(0.04.xxx, brdfLUT);
	coat.DirectionalAlbedo = saturate((0.04 * brdfLUT.x + brdfLUT.y) * coat.EnergyCompensation.x);
}

PreparedMaterialShading PrepareMaterialShading(SurfaceData surface,
	MaterialShadingFrame frame, float2 baseBrdfLUT, ClearcoatShadingState coat)
{
	PreparedMaterialShading material;
	material.Base = frame.Base;
	material.Anisotropy = frame.Anisotropy;
	material.ViewDirectionWS = frame.ViewDirectionWS;
	material.NoV = frame.NoV;
	material.BaseColor = surface.BaseColor;
	// The isotropic split-sum LUT estimates directional energy for anisotropy
	// using the base's effective roughness, without replacing either axis width.
	material.EnergyCompensation = GGXEnergyCompensation(frame.Base.F0, baseBrdfLUT);
	material.SpecularDirectionalAlbedo =
		saturate((frame.Base.F0 * baseBrdfLUT.x + baseBrdfLUT.y) * material.EnergyCompensation);
	// The view-integrated GGX albedo also weights direct diffuse. This remains
	// a directional approximation until incident-angle coupling is modeled.
	material.DiffuseWeight = (1.0.xxx - material.SpecularDirectionalAlbedo) * (1.0 - surface.Metallic);
	material.Clearcoat = coat;
	return material;
}

float3 EvaluateMaterialEmission(float3 emissive, ClearcoatShadingState coat)
{
	// Emission crosses the coat once; its outgoing Fresnel differs from the
	// squared directional-albedo transmission used for reflected IBL.
	const float coatFresnel = F_Schlick(0.04.xxx, 1.0.xxx, coat.NoV).x;
	return emissive * (1.0 - coat.Factor * coatFresnel);
}
