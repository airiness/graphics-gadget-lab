#ifndef __COMMON_INCLUDED__
#define __COMMON_INCLUDED__

static const float PI = 3.14159265359f;
static const float TWO_PI = 6.28318530718f;
static const float INV_PI = 0.31830988618f;
static const float HALF_PI = 1.57079632679f;

float Pow5(float x)
{
	float xx = x * x;
	return xx * xx * x;
}

#endif