#pragma once
#include <Common/Common.hlsli>

float D_GGX(float NoH, float a)
{
	float a2 = a * a;
	float f = (NoH * a2 - NoH) * NoH + 1.0;
	return a2 / (PI * f * f + 1e-5);
}

float3 F_Schlick(float3 F0, float3 F90, float cosTheta)
{
	return F0 + (F90 - F0) * Pow5(1.0 - cosTheta);
}

float V_SmithGGXCorrelated(float NoV, float NoL, float a)
{
	float a2 = a * a;
	float GGXL = NoV * sqrt((-NoL * a2 + NoL) * NoL + a2);
	float GGXV = NoL * sqrt((-NoV * a2 + NoV) * NoV + a2);
	return 0.5 / (GGXV + GGXL);
}

float3 Fd_Lambert(float3 DiffuseColor)
{
	return DiffuseColor * (1.0 / PI);
}