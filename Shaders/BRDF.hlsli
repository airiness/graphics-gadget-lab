#ifndef __BRDF_INCLUDED__
#define __BRDF_INCLUDED__

#include "Common.hlsli"

float D_GGX(float NoH, float a)
{
	float a2 = a * a;
	float f = (NoH * a2 - NoH) * NoH + 1.0;
	return a2 / (PI * f * f);
}

float3 F_Schlick(float u, float3 f0)
{
	return f0 + (float3(1.0) - f0) * Pow5(1.0 - u);
}


float V_SmithGGXCorrelated(float NoV, float NoL, float a)
{
	float a2 = a * a;
	float GGXL = NoV * sqrt((-NoL * a2 + NoL) * NoL + a2);
	float GGXV = NoL * sqrt((-NoV * a2 + NoV) * NoV + a2);
	return 0.5 / (GGXV + GGXL);
}

float Fd_Lambert()
{
	return 1.0 / PI;
}


#endif 