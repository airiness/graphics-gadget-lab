#pragma once
#include <Common/Common.hlsli>

float3 ResolveDisplayColor(float3 storedSceneColor, float exposureScaleOverPreExposure,
	float3 materialDiagnosticColor, float materialDiagnosticCoverage)
{
	const float3 litDisplayColor = ACESFitted(storedSceneColor * exposureScaleOverPreExposure);
	// Forward alpha blending has already premultiplied the diagnostic color and
	// its separate coverage in [0, 1]. The overlay never receives exposure or tone mapping.
	const float3 displayColor = litDisplayColor * (1.0 - materialDiagnosticCoverage) +
		materialDiagnosticColor;
	return LinearToSRGB(displayColor);
}
