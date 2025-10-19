#pragma once
static const float PI = 3.14159265359f;
static const float TWO_PI = 6.28318530718f;
static const float INV_PI = 0.31830988618f;
static const float HALF_PI = 1.57079632679f;

float Pow5(float x)
{
	float xx = x * x;
	return xx * xx * x;
}

// IEC 61966-2-1:2003
float3 SRGBToLinear(float c)
{
	return c < 0.04045 ? (c / 12.92) : pow((c + 0.055) / 1.055, 2.4);
}

float3 LinearToSRGB(float3 c)
{
	return (c < 0.0031308) ? (12.92 * c) : (1.055 * pow(c, 1.0 / 2.4) - 0.055);
}