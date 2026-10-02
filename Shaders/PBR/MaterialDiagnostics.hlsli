#pragma once
#include <Common/SurfaceEvaluation.hlsli>
#include <PBR/SpecularAA.hlsli>

struct MaterialDiagnosticOutput
{
	float4 Color;
	float4 Coverage;
	float4 Lighting;
};

MaterialDiagnosticOutput MakeMaterialDiagnosticOutput(float4 storedSceneColor,
	float3 diagnosticColor, bool diagnostic)
{
	MaterialDiagnosticOutput output;
	// Every MRT uses surface opacity, including lit surfaces that attenuate
	// diagnostics behind them. Lighting uses the same storage scale as scene color.
	output.Color = float4(diagnosticColor, storedSceneColor.a);
	output.Coverage = float4(diagnostic ? 1.0 : 0.0, 0.0, 0.0, storedSceneColor.a);
	output.Lighting = float4(diagnostic ? storedSceneColor.rgb : 0.0.xxx, storedSceneColor.a);
	return output;
}

// Parameter diagnostics are bounded linear Rec.709 colors, not scene radiance.
// Lit and UnfilteredLit deliberately fall through to ordinary lighting.
bool TryEvaluateMaterialDiagnostic(uint debugView, SurfaceData surface,
	BaseShadingState shading, SpecularAAResult coatSpecularAA,
	float3 clearcoatNormalWS, AnisotropyShadingState anisotropy, out float3 color)
{
	color = 0.0.xxx;
	switch (debugView)
	{
	case MaterialDebugViewBaseColor: color = surface.BaseColor; break;
	case MaterialDebugViewMetallic: color = surface.Metallic.xxx; break;
	case MaterialDebugViewRoughness:
		color = ClampPerceptualRoughnessForBRDF(surface.Roughness).xxx; break;
	case MaterialDebugViewNormal: color = shading.NormalWS * 0.5 + 0.5; break;
	case MaterialDebugViewAuthoredRoughness: color = shading.AuthoredPerceptualRoughness.xxx; break;
	case MaterialDebugViewEffectiveRoughness: color = shading.EffectivePerceptualRoughness.xxx; break;
	case MaterialDebugViewNormalVariance:
		color = float3(shading.NormalVariance,
			surface.ClearcoatFactor > 0.0 ? coatSpecularAA.NormalVariance : 0.0, 0.0); break;
	case MaterialDebugViewSpecularAAContribution:
		color = float3(shading.SpecularAAKernelAlpha,
			surface.ClearcoatFactor > 0.0 ? coatSpecularAA.KernelAlpha : 0.0,
			shading.EffectivePerceptualRoughness -
				ClampPerceptualRoughnessForBRDF(surface.Roughness)); break;
	case MaterialDebugViewF0: color = shading.F0; break;
	case MaterialDebugViewIor:
		color = (surface.Ior == 0.0 ? 1.0 : 1.0 - 1.0 / max(surface.Ior, 1.0)).xxx; break;
	case MaterialDebugViewClearcoatFactor: color = surface.ClearcoatFactor.xxx; break;
	case MaterialDebugViewClearcoatRoughness: color = surface.ClearcoatRoughness.xxx; break;
	case MaterialDebugViewEffectiveClearcoatRoughness:
		color = coatSpecularAA.EffectivePerceptualRoughness.xxx; break;
	case MaterialDebugViewClearcoatNormal: color = clearcoatNormalWS * 0.5 + 0.5; break;
	case MaterialDebugViewAnisotropyStrength: color = surface.AnisotropyStrength.xxx; break;
	case MaterialDebugViewAnisotropicAlpha: color = float3(anisotropy.AlphaT, anisotropy.AlphaB, 0.0); break;
	case MaterialDebugViewAnisotropyDirectionTangent:
		color = float3(surface.AnisotropyDirectionTS * 0.5 + 0.5, 0.5); break;
	case MaterialDebugViewAnisotropyDirectionWorld: color = anisotropy.TangentWS * 0.5 + 0.5; break;
	case MaterialDebugViewFeatureFlags:
		color = float3((shading.FeatureFlags & 1u) != 0u, (shading.FeatureFlags & 2u) != 0u, 0.0); break;
	default: return false;
	}
	color = saturate(color);
	return true;
}
