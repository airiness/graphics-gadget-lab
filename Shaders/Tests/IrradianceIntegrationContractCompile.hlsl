#include <Lighting/IrradianceIntegrationMath.hlsli>

float4 PSMain() : SV_Target0
{
#if GGLAB_IRRADIANCE_TEST_CASE == 0
	const bool matches = GetIrradianceSourceMip(64u, 256u, 9u) == 6u;
#elif GGLAB_IRRADIANCE_TEST_CASE == 1
	const bool matches = GetIrradianceSourceMip(256u, 512u, 10u) == 6u;
#elif GGLAB_IRRADIANCE_TEST_CASE == 2
	const bool matches = GetIrradianceSourceMip(1024u, 1024u, 11u) == 6u;
#elif GGLAB_IRRADIANCE_TEST_CASE == 3
	const bool matches = GetIrradianceSourceMip(4096u, 2048u, 12u) == 6u;
#elif GGLAB_IRRADIANCE_TEST_CASE == 4
	const bool matches = GetIrradianceSourceMip(256u, 512u, 3u) == 2u;
#elif GGLAB_IRRADIANCE_TEST_CASE == 5
	const bool matches = GetIrradianceSourceMip(65536u, 8u, 4u) == 0u;
#elif GGLAB_IRRADIANCE_TEST_CASE == 6
	const bool matches = GetIrradianceSourceMip(0u, 1u, 1u) == 0u;
#elif GGLAB_IRRADIANCE_TEST_CASE == 7
	const bool matches = abs(GetIrradianceNormalization(3.14159265359f) - 1.0f) < 1.0e-6;
#else
#error Unknown irradiance integration test case.
#endif
	return matches ? 0.0.xxxx : 1.0.xxxx;
}
