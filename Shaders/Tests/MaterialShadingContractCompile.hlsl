#include <PBR/IndirectLighting.hlsli>

// Zero reports success. Each expected value is derived from the literal LUT
// coefficients and the existing layer equations, independently of preparation.
static const float MaterialContractTolerance = 1.0e-5;

bool MatchesMaterialValue(float3 actual, float3 expected)
{
	return all(abs(actual - expected) < MaterialContractTolerance);
}

SurfaceData MakeContractSurface()
{
	SurfaceData surface = (SurfaceData)0;
	surface.BaseColor = float3(0.2, 0.4, 0.8);
	surface.Emissive = float3(0.5, 1.0, 2.0);
	surface.Metallic = 0.25;
	surface.ClearcoatFactor = 0.6;
	return surface;
}

MaterialShadingFrame MakeContractFrame()
{
	MaterialShadingFrame frame = (MaterialShadingFrame)0;
	frame.Base.NormalWS = float3(0.0, 0.0, 1.0);
	frame.Base.F0 = float3(0.04, 0.15, 0.7);
	frame.Base.AuthoredPerceptualRoughness = 0.25;
	frame.Base.EffectivePerceptualRoughness = 0.5;
	frame.Base.BRDFAlpha = 0.25;
	frame.Base.NormalVariance = 0.08;
	frame.Base.SpecularAAKernelAlpha = 0.01;
	frame.Base.FeatureFlags = 3u;
	frame.ClearcoatNormalWS = float3(0.0, 0.6, 0.8);
	frame.ClearcoatSpecularAA.EffectivePerceptualRoughness = 0.2;
	frame.Anisotropy.TangentWS = float3(1.0, 0.0, 0.0);
	frame.Anisotropy.BitangentWS = float3(0.0, 1.0, 0.0);
	frame.Anisotropy.AlphaT = 1.0;
	frame.Anisotropy.AlphaB = 0.5;
	frame.ViewDirectionWS = frame.Base.NormalWS;
	frame.NoV = 1.0;
	frame.ClearcoatNoV = 0.8;
	return frame;
}

MaterialIBLSamples MakeContractEnvironment()
{
	MaterialIBLSamples environment;
	environment.Irradiance = float3(2.0, 3.0, 4.0);
	environment.BaseSpecular = float3(5.0, 6.0, 7.0);
	environment.ClearcoatSpecular = float3(11.0, 13.0, 17.0);
	return environment;
}

PreparedMaterialShading PrepareContractMaterial(SurfaceData surface,
	MaterialShadingFrame frame, float2 baseBrdfLUT, float2 clearcoatBrdfLUT)
{
	ClearcoatShadingState coat = BuildClearcoatShadingState(surface, frame);
	if (coat.Factor > 0.0)
	{
		ApplyClearcoatDirectionalEnergy(coat, clearcoatBrdfLUT);
	}
	return PrepareMaterialShading(surface, frame, baseBrdfLUT, coat);
}

MaterialIBLResponse EvaluateContractMaterialIBL(PreparedMaterialShading material,
	MaterialIBLSamples environment)
{
	MaterialIBLResponse response = EvaluateBaseMaterialIBL(material, environment);
	if (material.Clearcoat.Factor > 0.0)
	{
		ApplyClearcoatIBL(response, material.Clearcoat, environment.ClearcoatSpecular);
	}
	return response;
}

float4 TestMaterialDirectionalEnergy() : SV_Target0
{
	const PreparedMaterialShading material = PrepareContractMaterial(MakeContractSurface(),
		MakeContractFrame(), float2(0.4, 0.1), float2(0.5, 0.1));
	// A+B=0.5 gives gain=1+F0. Albedo=(F0*A+B)*gain;
	// diffuse weight=(1-albedo)*(1-metallic), with metallic=0.25.
	const bool matches = MatchesMaterialValue(material.EnergyCompensation, float3(1.04, 1.15, 1.7)) &&
		MatchesMaterialValue(material.SpecularDirectionalAlbedo, float3(0.12064, 0.184, 0.646)) &&
		MatchesMaterialValue(material.DiffuseWeight, float3(0.65952, 0.612, 0.2655));
	return matches ? 0.0.xxxx : 1.0.xxxx;
}

float4 TestMetalSuppressesDiffuseIBL() : SV_Target0
{
	SurfaceData surface = MakeContractSurface();
	surface.Metallic = 1.0;
	surface.ClearcoatFactor = 0.0;
	MaterialShadingFrame frame = MakeContractFrame();
	frame.Base.F0 = surface.BaseColor;
	const PreparedMaterialShading material = PrepareContractMaterial(surface, frame,
		float2(0.4, 0.1), 0.0.xx);
	const MaterialIBLResponse response = EvaluateContractMaterialIBL(material, MakeContractEnvironment());
	// F0=base color and gain=1+F0 give albedo=(0.216, 0.364, 0.756).
	const bool matches = MatchesMaterialValue(response.Diffuse, 0.0.xxx) &&
		MatchesMaterialValue(response.Specular, float3(1.08, 2.184, 5.292));
	return matches ? 0.0.xxxx : 1.0.xxxx;
}

float4 TestClearcoatIBLTransmission() : SV_Target0
{
	const PreparedMaterialShading material = PrepareContractMaterial(MakeContractSurface(),
		MakeContractFrame(), float2(0.4, 0.1), float2(0.5, 0.1));
	const MaterialIBLResponse response = EvaluateContractMaterialIBL(material, MakeContractEnvironment());
	// Coat gain=1+0.04*(1/0.6-1); albedo=0.12*gain=0.1232.
	// Factor=0.6 gives crossing transmission=1-0.6*0.1232=0.92608.
	const float transmissionSquared = 0.92608 * 0.92608;
	const float3 expectedDiffuse = float3(0.263808, 0.7344, 0.8496) / PI * transmissionSquared;
	const float3 expectedSpecular = float3(0.6032, 1.104, 4.522) * transmissionSquared +
		float3(0.81312, 0.96096, 1.25664);
	const bool matches = abs(material.Clearcoat.DirectionalAlbedo - 0.1232) < MaterialContractTolerance &&
		MatchesMaterialValue(response.Diffuse, expectedDiffuse) &&
		MatchesMaterialValue(response.Specular, expectedSpecular);
	return matches ? 0.0.xxxx : 1.0.xxxx;
}

float4 TestDisabledClearcoatIgnoresSamples() : SV_Target0
{
	SurfaceData surface = MakeContractSurface();
	surface.ClearcoatFactor = 0.0;
	const PreparedMaterialShading material = PrepareContractMaterial(surface,
		MakeContractFrame(), float2(0.4, 0.1), float2(10.0, 20.0));
	MaterialIBLSamples environment = MakeContractEnvironment();
	environment.ClearcoatSpecular = 10000.0.xxx;
	const MaterialIBLResponse response = EvaluateContractMaterialIBL(material, environment);
	const bool matches = material.Clearcoat.DirectionalAlbedo == 0.0 &&
		MatchesMaterialValue(material.Clearcoat.EnergyCompensation, 1.0.xxx) &&
		MatchesMaterialValue(response.Diffuse, float3(0.263808, 0.7344, 0.8496) / PI) &&
		MatchesMaterialValue(response.Specular, float3(0.6032, 1.104, 4.522)) &&
		MatchesMaterialValue(EvaluateMaterialEmission(surface.Emissive, material.Clearcoat), surface.Emissive);
	return matches ? 0.0.xxxx : 1.0.xxxx;
}

float4 TestUnbakedMaterialLUT() : SV_Target0
{
	const PreparedMaterialShading material = PrepareContractMaterial(MakeContractSurface(),
		MakeContractFrame(), 0.0.xx, 0.0.xx);
	const MaterialIBLResponse response = EvaluateContractMaterialIBL(material, MakeContractEnvironment());
	// A=B=0 keeps unit compensation and zero directional albedo for both lobes.
	const bool matches = MatchesMaterialValue(material.EnergyCompensation, 1.0.xxx) &&
		MatchesMaterialValue(material.Clearcoat.EnergyCompensation, 1.0.xxx) &&
		MatchesMaterialValue(response.Diffuse, float3(0.3, 0.9, 2.4) / PI) &&
		MatchesMaterialValue(response.Specular, 0.0.xxx);
	return matches ? 0.0.xxxx : 1.0.xxxx;
}

float4 TestSaturatedMaterialAlbedo() : SV_Target0
{
	SurfaceData surface = MakeContractSurface();
	surface.ClearcoatFactor = 1.0;
	const PreparedMaterialShading material = PrepareContractMaterial(surface,
		MakeContractFrame(), float2(2.0, 2.0), float2(2.0, 2.0));
	const MaterialIBLSamples environment = MakeContractEnvironment();
	const MaterialIBLResponse response = EvaluateContractMaterialIBL(material, environment);
	// Saturated albedo suppresses base diffuse, and a unit coat replaces reflected base IBL.
	const bool matches = MatchesMaterialValue(material.SpecularDirectionalAlbedo, 1.0.xxx) &&
		MatchesMaterialValue(material.DiffuseWeight, 0.0.xxx) &&
		MatchesMaterialValue(response.Diffuse, 0.0.xxx) &&
		MatchesMaterialValue(response.Specular, environment.ClearcoatSpecular);
	return matches ? 0.0.xxxx : 1.0.xxxx;
}

float4 TestPreparedAnisotropicIBLDirection() : SV_Target0
{
	MaterialShadingFrame frame = MakeContractFrame();
	frame.ViewDirectionWS = float3(0.6, 0.0, 0.8);
	frame.NoV = 0.8;
	frame.ClearcoatNoV = 0.64;
	frame.Anisotropy.Strength = 0.7;
	const PreparedMaterialShading anisotropic = PrepareContractMaterial(MakeContractSurface(),
		frame, float2(0.4, 0.1), float2(0.5, 0.1));
	frame.Anisotropy.Strength = 0.0;
	const PreparedMaterialShading isotropic = PrepareContractMaterial(MakeContractSurface(),
		frame, float2(0.4, 0.1), float2(0.5, 0.1));
	// alphaB/alphaT=0.5 bends to the N/V half-vector, reflecting V to N.
	const bool matches = MatchesMaterialValue(GetMaterialIBLReflection(anisotropic), float3(0.0, 0.0, 1.0)) &&
		MatchesMaterialValue(GetMaterialIBLReflection(isotropic), float3(-0.6, 0.0, 0.8));
	return matches ? 0.0.xxxx : 1.0.xxxx;
}

float4 TestIndependentClearcoatFrame() : SV_Target0
{
	const PreparedMaterialShading material = PrepareContractMaterial(MakeContractSurface(),
		MakeContractFrame(), float2(0.4, 0.1), float2(0.5, 0.1));
	const bool matches = MatchesMaterialValue(material.Base.NormalWS, float3(0.0, 0.0, 1.0)) &&
		MatchesMaterialValue(material.Clearcoat.NormalWS, float3(0.0, 0.6, 0.8)) &&
		abs(material.Clearcoat.BRDFAlpha - 0.04) < MaterialContractTolerance &&
		material.NoV == 1.0 && material.Clearcoat.NoV == 0.8 &&
		material.Base.AuthoredPerceptualRoughness == 0.25 &&
		material.Base.EffectivePerceptualRoughness == 0.5 &&
		material.Base.FeatureFlags == 3u;
	return matches ? 0.0.xxxx : 1.0.xxxx;
}

float4 TestEmissionCrossesClearcoatOnce() : SV_Target0
{
	const SurfaceData surface = MakeContractSurface();
	const PreparedMaterialShading material = PrepareContractMaterial(surface,
		MakeContractFrame(), float2(0.4, 0.1), float2(0.5, 0.1));
	// NoV=0.8 gives Fresnel=0.04+0.96*0.2^5=0.0403072.
	// Emission uses one crossing with factor=0.6, not reflected IBL's squared transmission.
	const float emissionTransmission = 0.97581568;
	const bool matches = MatchesMaterialValue(EvaluateMaterialEmission(surface.Emissive, material.Clearcoat),
		surface.Emissive * emissionTransmission);
	return matches ? 0.0.xxxx : 1.0.xxxx;
}

float4 TestDisabledClearcoatSkipsFootprint(float3 normalWS : NORMAL) : SV_Target0
{
	MaterialData material = (MaterialData)0;
	material.DebugView = MaterialDebugViewLit;
	SurfaceData surface = (SurfaceData)0;
	surface.ClearcoatRoughness = 0.25;
	const SpecularAAResult result = PrepareClearcoatSpecularAA(material, surface, normalWS);
	// A varying normal must not generate a footprint for an unused coat.
	const bool matches = result.NormalVariance == 0.0 && result.KernelAlpha == 0.0 &&
		result.EffectivePerceptualRoughness == 0.25;
	return matches ? 0.0.xxxx : 1.0.xxxx;
}

float4 TestClearcoatNormalDiagnosticSkipsFootprint(float3 normalWS : NORMAL) : SV_Target0
{
	MaterialData material = (MaterialData)0;
	material.DebugView = MaterialDebugViewClearcoatNormal;
	SurfaceData surface = (SurfaceData)0;
	surface.ClearcoatRoughness = 0.5;
	const SpecularAAResult result = PrepareClearcoatSpecularAA(material, surface, normalWS);
	// Normal color diagnostics need the normal, but do not consume its AA footprint.
	const bool matches = result.NormalVariance == 0.0 && result.KernelAlpha == 0.0 &&
		result.EffectivePerceptualRoughness == 0.5;
	return matches ? 0.0.xxxx : 1.0.xxxx;
}

float4 TestClearcoatFootprintUsesUniformRequirements() : SV_Target0
{
	MaterialData material = (MaterialData)0;
	material.DebugView = MaterialDebugViewNormalVariance;
	const bool inactiveVariance = !NeedsClearcoatSpecularAA(material);
	material.DebugView = MaterialDebugViewSpecularAAContribution;
	const bool inactiveContribution = !NeedsClearcoatSpecularAA(material);
	material.DebugView = MaterialDebugViewEffectiveClearcoatRoughness;
	const bool requestedRoughness = NeedsClearcoatSpecularAA(material);
	material.DebugView = MaterialDebugViewLit;
	material.ClearcoatFactor = 1.0;
	// The requirement has no sampled-factor input: a textured zero cannot disable it.
	const bool activeCoat = NeedsClearcoatSpecularAA(material);
	material.DebugView = MaterialDebugViewUnfilteredLit;
	const bool unfilteredCoat = NeedsClearcoatSpecularAA(material);
	const bool matches = inactiveVariance && inactiveContribution && requestedRoughness && activeCoat && unfilteredCoat;
	return matches ? 0.0.xxxx : 1.0.xxxx;
}

float4 TestDisabledAnisotropySkipsRotation(float rotation : TEXCOORD0) : SV_Target0
{
	MaterialData material = (MaterialData)0;
	material.DebugView = MaterialDebugViewLit;
	material.AnisotropyRotation = rotation;
	// Retained bindings on a disabled layer must not cause sampling or direction work.
	material.AnisotropyBinding.TextureEnabled = 1u;
	const SurfaceData surface = EvaluateSurface(material, 0.0.xx, 0.0.xx);
	const bool matches = surface.AnisotropyStrength == 0.0 &&
		all(surface.AnisotropyDirectionTS == float2(1.0, 0.0));
	return matches ? 0.0.xxxx : 1.0.xxxx;
}

float4 TestZeroAnisotropyRetainsTangentDiagnostic() : SV_Target0
{
	MaterialData material = (MaterialData)0;
	material.DebugView = MaterialDebugViewAnisotropyDirectionTangent;
	material.AnisotropyRotation = PI * 0.5;
	const SurfaceData surface = EvaluateSurface(material, 0.0.xx, 0.0.xx);
	// A quarter-turn takes the untextured +T direction to +B even at zero strength.
	const bool matches = surface.AnisotropyStrength == 0.0 &&
		all(abs(surface.AnisotropyDirectionTS - float2(0.0, 1.0)) < MaterialContractTolerance);
	return matches ? 0.0.xxxx : 1.0.xxxx;
}

float4 TestZeroAnisotropyRetainsWorldDiagnostic() : SV_Target0
{
	MaterialData material = (MaterialData)0;
	material.DebugView = MaterialDebugViewAnisotropyDirectionWorld;
	material.AnisotropyRotation = -PI * 0.5;
	const SurfaceData surface = EvaluateSurface(material, 0.0.xx, 0.0.xx);
	const AnisotropyShadingState state = BuildAnisotropyShadingState(surface,
		float3(0.0, 0.0, 1.0), float3(0.0, 0.0, 1.0), float4(1.0, 0.0, 0.0, 1.0),
		0.0.xxx, 0.0.xx, 0.25, 0.0, true);
	// The negative quarter-turn maps +T to -B in this right-handed world frame.
	const bool matches = state.Strength == 0.0 &&
		MatchesMaterialValue(state.TangentWS, float3(0.0, -1.0, 0.0));
	return matches ? 0.0.xxxx : 1.0.xxxx;
}

float4 TestActiveAnisotropyUsesBindingFlag() : SV_Target0
{
	MaterialData material = (MaterialData)0;
	material.DebugView = MaterialDebugViewLit;
	material.AnisotropyStrength = 0.7;
	material.AnisotropyRotation = PI * 0.25;
	material.AnisotropyPadding = uint2(1u, 1u);
	const SurfaceData surface = EvaluateSurface(material, 0.0.xx, 0.0.xx);
	// Only the binding flag controls sampling. Reserved bytes must be ignored;
	// the untextured direction rotates by 45 degrees to (1/sqrt(2), 1/sqrt(2)).
	const bool matches = surface.AnisotropyStrength == 0.7 &&
		all(abs(surface.AnisotropyDirectionTS - 0.7071067812.xx) < MaterialContractTolerance);
	return matches ? 0.0.xxxx : 1.0.xxxx;
}
