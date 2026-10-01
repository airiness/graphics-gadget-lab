#pragma once

// Jacobian from cube-face UV area to solid angle at a direction.
inline float CubemapUvSolidAngleJacobian(float u, float v)
{
	const float x = u * 2.0f - 1.0f;
	const float y = v * 2.0f - 1.0f;
	const float lengthSquared = 1.0f + x * x + y * y;
	return 4.0f / (lengthSquared * sqrt(lengthSquared));
}

inline float GetSpecularEnvironmentPdf(float texelMass, float totalMass, uint resolution, float u, float v)
{
	return (texelMass / totalMass) * (float)(resolution * resolution) / CubemapUvSolidAngleJacobian(u, v);
}

// V == N in the split-sum prefilter, so the reflected GGX PDF is D(H) / 4.
inline float GetSpecularGGXPdf(float NoL, float alpha)
{
	const float alphaSquared = alpha * alpha;
	const float NoHSquared = (1.0f + NoL) * 0.5f;
	const float denominator = (1.0f - NoHSquared) + alphaSquared * NoHSquared;
	return alphaSquared / (12.56637061436f * denominator * denominator);
}

inline float GetSpecularMISWeight(float NoL, float ggxPdf, float environmentPdf, uint ggxCount, uint environmentCount)
{
	// Balance heuristic for the normalized integral of L * NoL * p_GGX.
	// At least one GGX sample is retained, giving support over the visible hemisphere.
	return NoL * ggxPdf / ((float)ggxCount * ggxPdf + (float)environmentCount * environmentPdf);
}
