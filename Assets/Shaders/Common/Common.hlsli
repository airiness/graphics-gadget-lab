#pragma once
#pragma pack_matrix(row_major) // Use row-major matrices
static const float PI = 3.14159265359f;
static const float TWO_PI = 6.28318530718f;
static const float INV_PI = 0.31830988618f;
static const float HALF_PI = 1.57079632679f;

float Pow5(float x)
{
	float xx = x * x;
	return xx * xx * x;
}

float3 ACESFitted(float3 x)
{
    // Narkowicz 2015
	const float a = 2.51;
	const float b = 0.03;
	const float c = 2.43;
	const float d = 0.59;
	const float e = 0.14;
	return saturate((x * (a * x + b)) / (x * (c * x + d) + e));
}

// Linear to sRGB Conversion
float3 LinearToSRGB(float3 c)
{
	c = max(c, 1.0e-8.xxx);
	const float3 t = step(0.0031308.xxx, c);
	const float3 lower = c * 12.92;
	const float3 higher = 1.055 * pow(c, 1.0 / 2.4) - 0.055;
	return lerp(lower, higher, t);
}

// sRGB to Linear Conversion
float3 SRGBToLinear(float3 c)
{
	c = max(c, 6.10352e-5.xxx);
	const float3 t = step(0.04045.xxx, c);
	const float3 lower = c * (1.0 / 12.92);
	const float3 higher = pow((c * (1.0 / 1.055) + 0.0521327), 2.4);
	return lerp(lower, higher, t);
}