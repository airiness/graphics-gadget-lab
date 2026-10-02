#include <PBR/AnisotropicIBL.hlsli>
#include <PBR/SpecularAA.hlsli>

// Constant-fold production HLSL, using analytic directions and invariants
// rather than a CPU copy of the bent-reflection algorithm.
float4 PSMain() : SV_Target0
{
	const float3 N = float3(0.0, 0.0, 1.0);
	const float3 V = float3(0.6, 0.0, 0.8);
	const float3 B = float3(0.0, 1.0, 0.0);
	float3 result;
	float3 expected;
#if GGLAB_ANISOTROPIC_IBL_TEST_CASE == 0
	result = GetAnisotropicIBLReflection(N, V, B, 0.1, 0.1);
	expected = float3(-0.6, 0.0, 0.8);
#elif GGLAB_ANISOTROPIC_IBL_TEST_CASE == 1
	result = GetAnisotropicIBLReflection(N, V, B, 1.0, 1.0);
	expected = float3(-0.6, 0.0, 0.8);
#elif GGLAB_ANISOTROPIC_IBL_TEST_CASE == 2
	result = GetAnisotropicIBLReflection(N, N, B, 1.0, 0.002025);
	expected = N;
#elif GGLAB_ANISOTROPIC_IBL_TEST_CASE == 3
	// Halfway between N and V gives their half-vector and reflects V to N.
	result = GetAnisotropicIBLReflection(N, V, B, 1.0, 0.5);
	expected = N;
#elif GGLAB_ANISOTROPIC_IBL_TEST_CASE == 4
	result = GetAnisotropicIBLReflection(N, V, float3(1.0, 0.0, 0.0), 1.0, 0.5);
	expected = float3(-0.6, 0.0, 0.8);
#elif GGLAB_ANISOTROPIC_IBL_TEST_CASE == 5
	result = GetAnisotropicIBLReflection(N, V, -B, 1.0, 0.5);
	expected = N;
#elif GGLAB_ANISOTROPIC_IBL_TEST_CASE == 6
	result = GetAnisotropicIBLReflection(N, B, B, 1.0, 0.002025);
	expected = -B;
#elif GGLAB_ANISOTROPIC_IBL_TEST_CASE == 7
	const float3 grazingView = float3(0.00002, 1.0, 0.00002);
	result = GetAnisotropicIBLReflection(N, grazingView, B, 1.0, 0.002025);
	expected = float3(-0.00002, -1.0, 0.00002);
#elif GGLAB_ANISOTROPIC_IBL_TEST_CASE == 8
	result = GetAnisotropicIBLReflection(N, V, B, 0.100001, 0.1);
	expected = float3(-0.6, 0.0, 0.8);
#elif GGLAB_ANISOTROPIC_IBL_TEST_CASE == 9
	const float2 authored = FilterAnisotropicAlpha(0.2, 0.8660254038, 0.0);
	const float2 filtered = FilterAnisotropicAlpha(0.2, 0.8660254038, 0.18);
	const float3 authoredReflection = GetAnisotropicIBLReflection(N, V, B, authored.x, authored.y);
	result = GetAnisotropicIBLReflection(N, V, B, filtered.x, filtered.y);
	const bool matches = abs(dot(result, result) - 1.0) < 1.0e-5 &&
		result.x < authoredReflection.x && result.x > -V.x;
	return matches ? 0.0.xxxx : 1.0.xxxx;
#elif GGLAB_ANISOTROPIC_IBL_TEST_CASE == 10
	result = GetAnisotropicIBLReflection(-N, float3(0.6, 0.0, -0.8), -B, 1.0, 0.5);
	expected = -N;
#else
#error Unknown anisotropic IBL test case.
#endif
#if GGLAB_ANISOTROPIC_IBL_TEST_CASE != 9
	// Ordered unit-length/error bounds reject NaN/Inf and constant-fold in DXC;
	// isfinite retains an intrinsic call even when its input is a literal.
	const bool matches = abs(dot(result, result) - 1.0) < 1.0e-5 &&
		all(abs(result - expected) < 3.0e-5);
	return matches ? 0.0.xxxx : 1.0.xxxx;
#endif
}
