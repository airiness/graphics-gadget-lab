#pragma once

#include <Common/HDRColorMath.hlsli>

static const float PI = 3.14159265359f;
static const float TWO_PI = 6.28318530718f;
static const float INV_PI = 0.31830988618f;
static const float HALF_PI = 1.57079632679f;

float Pow5(float x)
{
	float xx = x * x;
	return xx * xx * x;
}

float3 SanitizeHDRColor(float3 color)
{
	return float3(
		SanitizeHDRChannel(color.r),
		SanitizeHDRChannel(color.g),
		SanitizeHDRChannel(color.b));
}

float3 EncodeSceneColor(float3 sceneLinearColor, float preExposure)
{
	return float3(
		EncodeSceneColorChannel(sceneLinearColor.r, preExposure),
		EncodeSceneColorChannel(sceneLinearColor.g, preExposure),
		EncodeSceneColorChannel(sceneLinearColor.b, preExposure));
}

// Persistent FP32 physical lighting has no camera/storage scale or FP16 brightness ceiling.
float3 SanitizeSceneRadiance(float3 color)
{
	return float3(SanitizeSceneRadianceChannel(color.r),
		SanitizeSceneRadianceChannel(color.g), SanitizeSceneRadianceChannel(color.b));
}

float3 ACESFitted(float3 x)
{
	// Narkowicz 2015
	x = SanitizeHDRColor(x);
	const float a = 2.51;
	const float b = 0.03;
	const float c = 2.43;
	const float d = 0.59;
	const float e = 0.14;
	float3 numerator = x * (a * x + b);
	float3 denominator = x * (c * x + d) + e;
	return saturate(numerator / max(denominator, 1.0e-6.xxx));
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

// BuildTBNFromDerivatives()
// Reconstruct a tangent basis (T, B, N) per-pixel using screen-space derivatives.
// This is a common fallback when the mesh does NOT provide vertex tangents.
//
// Inputs:
//   N          : geometric normal in world space (should be normalized).
//   deltaPosX/Y : screen-space derivatives of world position.
//   deltaUVX/Y  : screen-space derivatives of the sampled texture coordinates.
//
// Output:
//   float3x3(T, B, N)  (rows are T, B, N; so mul(n_ts, tbn) works for row-vector convention)
//
// Notes / Caveats:
// - ddx/ddy are evaluated over a 2x2 pixel quad. If neighboring pixels take different
//   control-flow paths (dynamic branching, discard, etc.), derivatives can become unstable.
// - If UV mapping is degenerate (uv does not vary), the reconstructed basis is unreliable.
// - For mirrored UV islands, we need to handle handedness (sign) to keep normal maps consistent.
float3x3 BuildTBNFromDerivatives(float3 N, float3 deltaPosX, float3 deltaPosY,
	float2 deltaUVX, float2 deltaUVY)
{
	N = SafeNormalize(N, float3(0.0, 1.0, 0.0));

	// The goal is to estimate Tangent (T = ∂p/∂u) and Bitangent (B = -∂p/∂v).
	// Using the chain rule (locally, inside a triangle):
	//   ∂p/∂x = T * ∂u/∂x - B * ∂v/∂x
	//   ∂p/∂y = T * ∂u/∂y - B * ∂v/∂y
	//
	// Solve the UV Jacobian without dividing by its magnitude. Its sign preserves
	// mirrored UV orientation independently of screen winding or a flipped normal.
	const float uvDeterminant = deltaUVX.x * deltaUVY.y - deltaUVX.y * deltaUVY.x;
	const float orientation = uvDeterminant < 0.0 ? -1.0 : 1.0;
	float3 T = (deltaPosX * deltaUVY.y - deltaPosY * deltaUVX.y) * orientation;
	// glTF normal maps are +Y-up, opposite increasing V. Match imported tangents.
	float3 B = (deltaPosX * deltaUVY.x - deltaPosY * deltaUVX.x) * orientation;

	// A valid frame can be arbitrarily small as resolution or world units change.
	// Reject zero/non-finite frames, not a fixed world-space magnitude.
	float tLen2 = dot(T, T);
	float bLen2 = dot(B, B);
	const float frameLen2 = max(tLen2, bLen2);
	if (uvDeterminant == 0.0 || frameLen2 <= 0.0 ||
		(asuint(frameLen2) & 0x7f800000u) == 0x7f800000u)
	{
		float3 up = (abs(N.y) < 0.999) ? float3(0.0, 1.0, 0.0) : float3(0.0, 0.0, 1.0);
		T = SafeNormalize(cross(up, N), float3(1.0, 0.0, 0.0));
		B = SafeNormalize(cross(N, T), float3(0.0, 0.0, 1.0));
		return float3x3(T, B, N);
	}

	// We compute a safe scale to avoid division by zero.
	float invMax = rsqrt(frameLen2);

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

float3x3 BuildTBN(float3 N, float3 positionWS, float2 uv)
{
	return BuildTBNFromDerivatives(N, ddx(positionWS), ddy(positionWS), ddx(uv), ddy(uv));
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
