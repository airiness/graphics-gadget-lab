#include <Lighting/IrradianceIntegrationMath.hlsli>

// Each entry returns zero on success and one on failure; the CPU checks the optimized DXIL outputs.
float4 TestIrradianceGridForLowPreset() : SV_Target0
{
	const uint sampleBudget = 64u;
	const uint environmentFaceResolution = 256u;
	const uint availableMipLevels = 9u; // Full chain: log2(256) + 1.
	// Mip 6 yields 4x4 texels per face: 6 * 4^2 = 96 directions cover the budget.
	const uint expectedSourceMip = 6u;
	const uint actualSourceMip = GetIrradianceSourceMip(sampleBudget, environmentFaceResolution, availableMipLevels);
	const bool matches = actualSourceMip == expectedSourceMip;
	return matches ? 0.0.xxxx : 1.0.xxxx;
}

float4 TestIrradianceGridForMediumPreset() : SV_Target0
{
	const uint sampleBudget = 256u;
	const uint environmentFaceResolution = 512u;
	const uint availableMipLevels = 10u; // Full chain: log2(512) + 1.
	// Mip 6 yields 8x8 texels per face: 6 * 8^2 = 384 directions cover the budget.
	const uint expectedSourceMip = 6u;
	const uint actualSourceMip = GetIrradianceSourceMip(sampleBudget, environmentFaceResolution, availableMipLevels);
	const bool matches = actualSourceMip == expectedSourceMip;
	return matches ? 0.0.xxxx : 1.0.xxxx;
}

float4 TestIrradianceGridForHighPreset() : SV_Target0
{
	const uint sampleBudget = 1024u;
	const uint environmentFaceResolution = 1024u;
	const uint availableMipLevels = 11u; // Full chain: log2(1024) + 1.
	// Mip 6 yields 16x16 texels per face: 6 * 16^2 = 1536 directions cover the budget.
	const uint expectedSourceMip = 6u;
	const uint actualSourceMip = GetIrradianceSourceMip(sampleBudget, environmentFaceResolution, availableMipLevels);
	const bool matches = actualSourceMip == expectedSourceMip;
	return matches ? 0.0.xxxx : 1.0.xxxx;
}

float4 TestIrradianceGridForOfflinePreset() : SV_Target0
{
	const uint sampleBudget = 4096u;
	const uint environmentFaceResolution = 2048u;
	const uint availableMipLevels = 12u; // Full chain: log2(2048) + 1.
	// Mip 6 yields 32x32 texels per face: 6 * 32^2 = 6144 directions cover the budget.
	const uint expectedSourceMip = 6u;
	const uint actualSourceMip = GetIrradianceSourceMip(sampleBudget, environmentFaceResolution, availableMipLevels);
	const bool matches = actualSourceMip == expectedSourceMip;
	return matches ? 0.0.xxxx : 1.0.xxxx;
}

float4 TestIrradianceGridWithTruncatedMipChain() : SV_Target0
{
	const uint sampleBudget = 256u;
	const uint environmentFaceResolution = 512u;
	const uint availableMipLevels = 3u; // Only mips 0 through 2 exist.
	// The desired 8x8 face grid is unavailable; select the last existing mip.
	const uint expectedSourceMip = 2u;
	const uint actualSourceMip = GetIrradianceSourceMip(sampleBudget, environmentFaceResolution, availableMipLevels);
	const bool matches = actualSourceMip == expectedSourceMip;
	return matches ? 0.0.xxxx : 1.0.xxxx;
}

float4 TestIrradianceGridWithInsufficientSourceResolution() : SV_Target0
{
	const uint sampleBudget = 65536u;
	const uint environmentFaceResolution = 8u;
	const uint availableMipLevels = 4u; // Full chain: log2(8) + 1.
	// The requested face grid is ceil(sqrt(65536 / 6)) = 105, larger than mip 0's 8x8 grid.
	const uint expectedSourceMip = 0u;
	const uint actualSourceMip = GetIrradianceSourceMip(sampleBudget, environmentFaceResolution, availableMipLevels);
	const bool matches = actualSourceMip == expectedSourceMip;
	return matches ? 0.0.xxxx : 1.0.xxxx;
}

float4 TestIrradianceGridWithZeroBudget() : SV_Target0
{
	const uint sampleBudget = 0u;
	const uint environmentFaceResolution = 1u;
	const uint availableMipLevels = 1u; // Only mip 0 exists.
	// A zero budget still requests at least one texel per face.
	const uint expectedSourceMip = 0u;
	const uint actualSourceMip = GetIrradianceSourceMip(sampleBudget, environmentFaceResolution, availableMipLevels);
	const bool matches = actualSourceMip == expectedSourceMip;
	return matches ? 0.0.xxxx : 1.0.xxxx;
}

float4 TestIrradianceNormalizationForConstantEnvironment() : SV_Target0
{
	// The hemisphere integral of cos(theta) is pi, so its normalization factor is pi / pi = 1.
	const float hemisphereCosineIntegral = 3.14159265359f;
	const float expectedNormalization = 1.0f;
	const float absoluteTolerance = 1.0e-6f;
	const float actualNormalization = GetIrradianceNormalization(hemisphereCosineIntegral);
	const bool matches = abs(actualNormalization - expectedNormalization) < absoluteTolerance;
	return matches ? 0.0.xxxx : 1.0.xxxx;
}
