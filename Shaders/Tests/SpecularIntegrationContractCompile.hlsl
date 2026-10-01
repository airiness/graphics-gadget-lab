#include <Lighting/SpecularIntegrationMath.hlsli>

float4 PSMain() : SV_Target0
{
#if GGLAB_SPECULAR_TEST_CASE == 0
	const bool matches = abs(CubemapUvSolidAngleJacobian(0.5, 0.5) - 4.0) < 0.00001;
#elif GGLAB_SPECULAR_TEST_CASE == 1
	const bool matches = abs(CubemapUvSolidAngleJacobian(0.0, 0.0) - 0.76980036) < 0.00001;
#elif GGLAB_SPECULAR_TEST_CASE == 2
	const bool matches = abs(GetSpecularGGXPdf(0.25, 1.0) - 0.07957747) < 0.00001;
#elif GGLAB_SPECULAR_TEST_CASE == 3
	const bool matches = abs(GetSpecularGGXPdf(1.0, 0.25) - 1.2732395) < 0.00001;
#elif GGLAB_SPECULAR_TEST_CASE == 4
	const bool matches = abs(GetSpecularEnvironmentPdf(2.0, 8.0, 4u, 0.5, 0.5) - 1.0) < 0.00001;
#elif GGLAB_SPECULAR_TEST_CASE == 5
	const bool matches = abs(GetSpecularMISWeight(0.5, 0.25, 0.5, 3u, 2u) - 0.07142857) < 0.00001;
#elif GGLAB_SPECULAR_TEST_CASE == 6
	const bool matches = abs(GetSpecularMISWeight(0.5, 0.25, 0.0, 1u, 0u) - 0.5) < 0.00001;
#else
	const bool matches = GetSpecularEnvironmentPdf(0.0, 8.0, 4u, 0.5, 0.5) == 0.0;
#endif
	return matches ? 0.0.xxxx : 1.0.xxxx;
}
