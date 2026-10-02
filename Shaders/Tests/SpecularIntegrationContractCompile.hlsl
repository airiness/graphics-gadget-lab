#include <Lighting/SpecularIntegrationMath.hlsli>

// Each entry returns zero on success and one on failure; the CPU checks the optimized DXIL outputs.
// Absolute tolerance for the single-precision scalar contracts below.
static const float AbsoluteTolerance = 1.0e-5f;

float4 TestCubemapJacobianAtFaceCenter() : SV_Target0
{
	const float faceCenterUV = 0.5f;
	// Projected x = y = 0: J = 4 / (1 + x*x + y*y)^(3/2) = 4.
	const float expectedJacobian = 4.0f;
	const float actualJacobian = CubemapUvSolidAngleJacobian(faceCenterUV, faceCenterUV);
	const bool matches = abs(actualJacobian - expectedJacobian) < AbsoluteTolerance;
	return matches ? 0.0.xxxx : 1.0.xxxx;
}

float4 TestCubemapJacobianAtFaceCorner() : SV_Target0
{
	const float faceCornerUV = 0.0f;
	// Projected x = y = -1: J = 4 / (3 * sqrt(3)).
	const float expectedJacobian = 0.76980036f;
	const float actualJacobian = CubemapUvSolidAngleJacobian(faceCornerUV, faceCornerUV);
	const bool matches = abs(actualJacobian - expectedJacobian) < AbsoluteTolerance;
	return matches ? 0.0.xxxx : 1.0.xxxx;
}

float4 TestGGXPdfAtUnitAlpha() : SV_Target0
{
	const float NoL = 0.25f;
	const float alpha = 1.0f;
	// With V == N and alpha == 1, D = 1 / pi and the reflected-direction PDF is 1 / (4 * pi).
	const float expectedPdf = 0.07957747f;
	const float actualPdf = GetSpecularGGXPdf(NoL, alpha);
	const bool matches = abs(actualPdf - expectedPdf) < AbsoluteTolerance;
	return matches ? 0.0.xxxx : 1.0.xxxx;
}

float4 TestGGXPdfAtNormalIncidence() : SV_Target0
{
	const float NoL = 1.0f;
	const float alpha = 0.25f;
	// With V == N and H == N, the reflected-direction PDF is 1 / (4 * pi * alpha^2).
	const float expectedPdf = 1.2732395f;
	const float actualPdf = GetSpecularGGXPdf(NoL, alpha);
	const bool matches = abs(actualPdf - expectedPdf) < AbsoluteTolerance;
	return matches ? 0.0.xxxx : 1.0.xxxx;
}

float4 TestEnvironmentPdfAtFaceCenter() : SV_Target0
{
	const float texelMass = 2.0f;
	const float totalMass = 8.0f;
	const uint faceResolution = 4u;
	const float faceCenterUV = 0.5f;
	// Cell probability is 2/8, UV density is 4^2, and the center Jacobian is 4: PDF = 1.
	const float expectedPdf = 1.0f;
	const float actualPdf = GetSpecularEnvironmentPdf(texelMass, totalMass, faceResolution, faceCenterUV, faceCenterUV);
	const bool matches = abs(actualPdf - expectedPdf) < AbsoluteTolerance;
	return matches ? 0.0.xxxx : 1.0.xxxx;
}

float4 TestMISWeightWithBothProposals() : SV_Target0
{
	const float NoL = 0.5f;
	const float ggxPdf = 0.25f;
	const float environmentPdf = 0.5f;
	const uint ggxCount = 3u;
	const uint environmentCount = 2u;
	// Weight = (0.5 * 0.25) / (3 * 0.25 + 2 * 0.5) = 1/14.
	const float expectedWeight = 0.07142857f;
	const float actualWeight = GetSpecularMISWeight(NoL, ggxPdf, environmentPdf, ggxCount, environmentCount);
	const bool matches = abs(actualWeight - expectedWeight) < AbsoluteTolerance;
	return matches ? 0.0.xxxx : 1.0.xxxx;
}

float4 TestMISWeightWithGGXOnly() : SV_Target0
{
	const float NoL = 0.5f;
	const float ggxPdf = 0.25f;
	const float environmentPdf = 0.0f;
	const uint ggxCount = 1u;
	const uint environmentCount = 0u;
	// With one GGX sample and no environment samples, the PDF cancels and weight = NoL.
	const float expectedWeight = 0.5f;
	const float actualWeight = GetSpecularMISWeight(NoL, ggxPdf, environmentPdf, ggxCount, environmentCount);
	const bool matches = abs(actualWeight - expectedWeight) < AbsoluteTolerance;
	return matches ? 0.0.xxxx : 1.0.xxxx;
}

float4 TestEnvironmentPdfWithZeroMass() : SV_Target0
{
	const float texelMass = 0.0f;
	const float totalMass = 8.0f;
	const uint faceResolution = 4u;
	const float faceCenterUV = 0.5f;
	// A zero-mass cell has zero probability within a nonempty distribution.
	const float expectedPdf = 0.0f;
	const float actualPdf = GetSpecularEnvironmentPdf(texelMass, totalMass, faceResolution, faceCenterUV, faceCenterUV);
	const bool matches = actualPdf == expectedPdf;
	return matches ? 0.0.xxxx : 1.0.xxxx;
}
