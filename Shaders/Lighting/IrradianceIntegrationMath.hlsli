#pragma once

// Select an existing mip whose six face grids cover the requested sample budget.
// The budget controls grid density; only the visible hemisphere needs texture reads.
uint GetIrradianceSourceMip(uint sampleBudget, uint environmentResolution, uint environmentMipLevels)
{
	const uint targetSide = max(1u, (uint)ceil(sqrt((float)max(sampleBudget, 1u) / 6.0f)));
	uint sourceMip = 0u;
	while (sourceMip + 1u < environmentMipLevels &&
		(environmentResolution >> (sourceMip + 1u)) >= targetSide)
	{
		++sourceMip;
	}
	return sourceMip;
}

float CubemapSolidAnglePrimitive(float x, float y)
{
	return atan2(x * y, sqrt(x * x + y * y + 1.0f));
}

// Integrate the cube-face projection Jacobian over the complete texel rectangle.
// Equal UV areas cover different solid angles near face centers and corners.
float GetIrradianceTexelSolidAngle(uint x, uint y, uint resolution)
{
	const float width = 2.0f / (float)resolution;
	const float minX = (float)x * width - 1.0f;
	const float minY = (float)y * width - 1.0f;
	const float maxX = minX + width;
	const float maxY = minY + width;
	return CubemapSolidAnglePrimitive(maxX, maxY) - CubemapSolidAnglePrimitive(minX, maxY) -
		CubemapSolidAnglePrimitive(maxX, minY) + CubemapSolidAnglePrimitive(minX, minY);
}

float GetIrradianceNormalization(float cosineIntegral)
{
	// The full six-face grid gives a positive integral for every unit normal.
	// Calibrate finite-grid cosine weights so a constant environment has E = PI * L.
	return 3.14159265359f / cosineIntegral;
}
