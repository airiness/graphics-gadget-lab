#pragma once

// Scalar-only so the integration suite exercises the production equations against
// a sampled plane oracle without duplicating shader policy in the test.
float ShadowBilinearReceiverBias(float depthDu, float depthDv, float texelU, float texelV)
{
	return abs(depthDu) * texelU + abs(depthDv) * texelV;
}

float OffsetShadowReceiverDepth(float depth, float depthDu, float depthDv, float offsetU, float offsetV)
{
	return depth + depthDu * offsetU + depthDv * offsetV;
}
