#pragma once
#include <Common/Common.hlsli>

// Bent-reflection approximation for an isotropically prefiltered environment.
// N, V and B are unit vectors; B is perpendicular to N, and the filtered GGX
// widths satisfy 0 < alphaB <= alphaT. Their contrast vanishes for isotropic
// or fully rough lobes and decreases as specular AA broadens both axes.
float3 GetAnisotropicIBLReflection(float3 N, float3 V, float3 B, float alphaT, float alphaB)
{
	const float3 projectedView = V - B * dot(B, V);
	// A view along B has no projected direction; retain the shading normal.
	const float3 anisotropicNormal = SafeNormalize(projectedView, N);
	const float bendWeight = 1.0 - alphaB / alphaT;
	const float3 bentNormal = SafeNormalize(lerp(N, anisotropicNormal, bendWeight), N);
	return reflect(-V, bentNormal);
}
