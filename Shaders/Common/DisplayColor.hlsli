#pragma once
#include <Common/Common.hlsli>

float3 ResolveDisplayColor(float3 storedSceneColor, float exposureScaleOverPreExposure,
	float3 materialDiagnosticColor, float materialDiagnosticCoverage,
	float3 storedDiagnosticLighting = 0.0.xxx)
{
	if (materialDiagnosticCoverage <= 0.0)
	{
		return LinearToSRGB(ACESFitted(storedSceneColor * exposureScaleOverPreExposure));
	}
	const float litCoverage = 1.0 - saturate(materialDiagnosticCoverage);
	float3 litDisplayColor = 0.0.xxx;
	if (litCoverage > 0.0)
	{
		// Remove diagnostic surfaces' lighting, then undo its coverage before
		// tone mapping. Otherwise transparency retains that lighting or weights
		// the residual background twice. All three diagnostic MRTs are premultiplied.
		const float3 residualLighting = max(storedSceneColor - storedDiagnosticLighting, 0.0.xxx);
		litDisplayColor = ACESFitted(residualLighting *
			(exposureScaleOverPreExposure / litCoverage)) * litCoverage;
	}
	return LinearToSRGB(litDisplayColor + materialDiagnosticColor);
}
