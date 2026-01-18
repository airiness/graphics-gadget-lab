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

// Get texture by descriptor index
Texture2D<float4> GetTexture2D(uint srvIndex)
{
	return ResourceDescriptorHeap[NonUniformResourceIndex(srvIndex)];
}

// Get sampler by descriptor index
SamplerState GetSamplerState(uint samplerIndex)
{
	return SamplerDescriptorHeap[NonUniformResourceIndex(samplerIndex)];
}

// Sampler texture2D with srv and sampler indices
float4 SampleTexture2D(uint srvIndex, uint samplerIndex, float2 uv)
{
	Texture2D<float4> tex = GetTexture2D(srvIndex);
	SamplerState samp = GetSamplerState(samplerIndex);
	return tex.Sample(samp, uv);
}

// Sampler texture2D with srv index and sampler state
float4 SampleTexture2D(uint srvIndex, SamplerState samplerState, float2 uv)
{
	Texture2D<float4> tex = GetTexture2D(srvIndex);
	return tex.Sample(samplerState, uv);
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
    // We compute a safe scale to avoid division by zero.
	float tLen2 = dot(T, T);
	float bLen2 = dot(B, B);
	float invMax = rsqrt(max(tLen2, bLen2) + 1e-8);

	T *= invMax;
	B *= invMax;

    // --- Orthonormalize and fix handedness --------------------
    // 1) Make T orthogonal to N (Gram-Schmidt). This helps reduce skew due to interpolation.
	T = normalize(T - N * dot(N, T));

    // 2) Determine handedness sign to handle mirrored UVs:
    //    If (T,B,N) form a left-handed basis, flip B.
    //    sign = +1 for right-handed, -1 for left-handed.
	float handedness = (dot(cross(T, B), N) < 0.0) ? -1.0 : 1.0;

    // 3) Recompute B from N and T to guarantee orthogonality, then apply handedness.
	B = normalize(cross(N, T)) * handedness;

    // Return TBN basis.
    // IMPORTANT: This uses row-vector convention:
    //   n_ws = normalize(mul(n_ts, float3x3(T,B,N)));
	return float3x3(T, B, N);
}