#pragma once
#include "GIShaderConstants.hlsli"

#if defined(__cplusplus)
namespace gglab::gi_shader
{
#endif

// Fixed integer avalanche and Weyl increment preserve the qualified sample streams.
static uint GIHash(uint value)
{
	value ^= value >> 16;
	value *= 0x7feb352du;
	value ^= value >> 15;
	value *= 0x846ca68bu;
	return value ^ (value >> 16);
}

static float GIUniformOpenInterval(uint bits)
{
	// Keep 23 bits: the half-unit is representable, so neither endpoint rounds to 1.
	const uint mantissaBits = 23;
	const uint discardedBits = 32 - mantissaBits;
	return ((float)(bits >> discardedBits) + 0.5f) / (float)(1u << mantissaBits);
}

#if defined(__cplusplus)
static float GIRandom(uint& state)
#else
float GIRandom(inout uint state)
#endif
{
	state = GIHash(state + 0x9e3779b9u);
	return GIUniformOpenInterval(state);
}

// Two-dimensional Sobol digital net in Gray-code order. Keep integer construction
// shared with CPU contracts; fixed rotations preserve contiguous-block stratification.
static uint GIProgressiveSampleBits(uint index, uint dimension)
{
	uint gray = index ^ (index >> 1);
	uint direction = 0x80000000u;
	uint result = 0;
	while (gray != 0)
	{
		if ((gray & 1u) != 0)
		{
			result ^= direction;
		}
		gray >>= 1;
		direction = dimension == 0 ? direction >> 1 : direction ^ (direction >> 1);
	}
	return result;
}

#if defined(__cplusplus)
}
#endif
