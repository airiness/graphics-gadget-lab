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

float3 SafeNormalize(float3 v, float3 fallback)
{
	float len2 = dot(v, v);
	return (len2 > 1.0e-8) ? v * rsqrt(len2) : fallback;
}

// BuildTBN()
// Reconstruct a tangent basis (T, B, N) per-pixel using screen-space derivatives.
// This is a common fallback when the mesh does NOT provide vertex tangents.
//
// Inputs:
//   N          : geometric normal in world space (should be normalized).
//   positionWS : world-space position at current pixel (interpolated).
//   uv         : texture coordinates at current pixel (interpolated).
//
// Output:
//   float3x3(T, B, N)  (rows are T, B, N; so mul(n_ts, tbn) works for row-vector convention)
//
// Notes / Caveats:
// - ddx/ddy are evaluated over a 2x2 pixel quad. If neighboring pixels take different
//   control-flow paths (dynamic branching, discard, etc.), derivatives can become unstable.
// - If UV mapping is degenerate (uv does not vary), the reconstructed basis is unreliable.
// - For mirrored UV islands, we need to handle handedness (sign) to keep normal maps consistent.
float3x3 BuildTBN(float3 N, float3 positionWS, float2 uv)
{
	N = SafeNormalize(N, float3(0.0, 1.0, 0.0));

    // Screen-space partial derivatives of world position:
    //   deltaPosX ≈ ∂positionWS / ∂screenX   (difference to the right neighbor pixel)
    //   deltaPosY ≈ ∂positionWS / ∂screenY   (difference to the bottom neighbor pixel)
	float3 deltaPosX = ddx(positionWS);
	float3 deltaPosY = ddy(positionWS);

    // Screen-space partial derivatives of UV:
    //   deltaUVX ≈ ∂uv / ∂screenX
    //   deltaUVY ≈ ∂uv / ∂screenY
	float2 deltaUVX = ddx(uv);
	float2 deltaUVY = ddy(uv);

    // The goal is to estimate Tangent (T = ∂p/∂u) and Bitangent (B = ∂p/∂v).
    // Using the chain rule (locally, inside a triangle):
    //   ∂p/∂x = T * ∂u/∂x + B * ∂v/∂x
    //   ∂p/∂y = T * ∂u/∂y + B * ∂v/∂y
    //
    // One can solve this 2x2 linear system for T and B. The following cross-product form
    // is a numerically stable rearrangement that also tends to keep vectors in the tangent plane.

    // Build two vectors guaranteed to be perpendicular to N (i.e., lie in the tangent plane).
    // cross(deltaPosY, N) and cross(N, deltaPosX) are both orthogonal to N.
	float3 deltaPosYPerp = cross(deltaPosY, N);
	float3 deltaPosXPerp = cross(N, deltaPosX);

    // Combine them with UV derivatives to produce unnormalized T and B.
    // Intuition: these terms implement the "adjugate" of the 2x2 UV derivative matrix,
    // avoiding an explicit inverse, while staying in the tangent plane.
	float3 T = deltaPosYPerp * deltaUVX.x + deltaPosXPerp * deltaUVY.x;
	float3 B = deltaPosYPerp * deltaUVX.y + deltaPosXPerp * deltaUVY.y;

    // If the triangle/UV mapping is degenerate, T/B may become very small.
	float tLen2 = dot(T, T);
	float bLen2 = dot(B, B);
	if (max(tLen2, bLen2) <= 1.0e-8)
	{
		float3 up = (abs(N.y) < 0.999) ? float3(0.0, 1.0, 0.0) : float3(0.0, 0.0, 1.0);
		T = SafeNormalize(cross(up, N), float3(1.0, 0.0, 0.0));
		B = SafeNormalize(cross(N, T), float3(0.0, 0.0, 1.0));
		return float3x3(T, B, N);
	}

    // We compute a safe scale to avoid division by zero.
	float invMax = rsqrt(max(tLen2, bLen2) + 1e-8);

	T *= invMax;
	B *= invMax;

    // --- Orthonormalize and fix handedness --------------------
    // 1) Make T orthogonal to N (Gram-Schmidt). This helps reduce skew due to interpolation.
	T = T - N * dot(N, T);
	T = SafeNormalize(T, float3(1.0, 0.0, 0.0));

    // 2) Determine handedness sign to handle mirrored UVs:
    //    If (T,B,N) form a left-handed basis, flip B.
    //    sign = +1 for right-handed, -1 for left-handed.
	float handedness = (dot(cross(T, B), N) < 0.0) ? -1.0 : 1.0;

    // 3) Recompute B from N and T to guarantee orthogonality, then apply handedness.
	B = SafeNormalize(cross(N, T), float3(0.0, 0.0, 1.0)) * handedness;

    // Return TBN basis.
    // IMPORTANT: This uses row-vector convention:
    //   n_ws = normalize(mul(n_ts, float3x3(T,B,N)));
	return float3x3(T, B, N);
}

float3x3 BuildTBNFromTangent(float3 N, float4 tangentWS, float3 positionWS, float2 uv)
{
	N = SafeNormalize(N, float3(0.0, 1.0, 0.0));

	float3 T = tangentWS.xyz;
	if (dot(T, T) <= 1.0e-8)
	{
		return BuildTBN(N, positionWS, uv);
	}

	T = T - N * dot(N, T);
	if (dot(T, T) <= 1.0e-8)
	{
		return BuildTBN(N, positionWS, uv);
	}

	T = normalize(T);
	float handedness = (tangentWS.w < 0.0) ? -1.0 : 1.0;
	float3 B = SafeNormalize(cross(N, T), float3(0.0, 0.0, 1.0)) * handedness;
	return float3x3(T, B, N);
}
