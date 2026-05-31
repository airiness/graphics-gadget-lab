#pragma once
#include <Common/Common.hlsli>

// Reverses the bit order of a 32-bit unsigned integer.
//
// Example using a shortened 8-bit representation:
//   input : 00000110b
//   output: 01100000b
uint ReverseBits32(uint bits)
{
	bits = (bits << 16) | (bits >> 16);
	bits = ((bits & 0x00ff00ffu) << 8) | ((bits & 0xff00ff00u) >> 8);
	bits = ((bits & 0x0f0f0f0fu) << 4) | ((bits & 0xf0f0f0f0u) >> 4);
	bits = ((bits & 0x33333333u) << 2) | ((bits & 0xccccccccu) >> 2);
	bits = ((bits & 0x55555555u) << 1) | ((bits & 0xaaaaaaaau) >> 1);
	return bits;
}

// Computes the base-2 radical inverse, also known as the Van der Corput sequence in base 2.
//
// Conceptually, the binary digits of the integer are reversed and interpreted as fractional bits.
//
// Example:
//   6 = 110b -> 0.011b = 0.375
float RadicalInverseVdC(uint bits)
{
	bits = ReverseBits32(bits);
	return float(bits) * 2.3283064365386963e-10; // 1 / 2^32
}

// Returns the 2D Hammersley sample point for index i out of N samples.
//
// The sequence is:
//   (i / N, RadicalInverse_VdC(i))
//
// This produces a deterministic low-discrepancy point in [0, 1)^2, useful for
// importance sampling and numerical integration.
float2 Hammersley(uint i, uint N)
{
	return float2((float) i / (float) N, RadicalInverseVdC(i));
}

// Returns a cosine-weighted direction on the positive-Z tangent-space hemisphere.
float3 CosineSampleHemisphere(float2 Xi)
{
	float sinTheta = sqrt(Xi.y);
	float cosTheta = sqrt(1.0 - Xi.y);

	float phi = TWO_PI * Xi.x;
	float sinPhi;
	float cosPhi;
	sincos(phi, sinPhi, cosPhi);
	
	return float3(
		cosPhi * sinTheta,
		sinPhi * sinTheta,
		cosTheta);
}

// Builds an orthonormal basis around normalWS and transforms a tangent-space direction to world space.
// normalWS is expected to be normalized.
float3 TangentToWorld(float3 directionTS, float3 normalWS)
{
	float3 up = abs(normalWS.y) < 0.999
		? float3(0.0, 1.0, 0.0)
		: float3(0.0, 0.0, 1.0);
	float3 tangent = normalize(cross(up, normalWS));
	float3 bitangent = cross(normalWS, tangent);

	return normalize(
		directionTS.x * tangent +
		directionTS.y * bitangent +
		directionTS.z * normalWS);
}
