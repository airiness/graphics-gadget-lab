#pragma once
#pragma pack_matrix(row_major)

float2 PixelCenterToUV(uint2 pixel, uint2 extent)
{
	return (float2(pixel) + 0.5) / max(float2(extent), 1.0.xx);
}

float2 UVToNDC(float2 uv)
{
	return float2(
		uv.x * 2.0 - 1.0,
		1.0 - uv.y * 2.0);
}

float2 NDCToUV(float2 ndc)
{
	return float2(
		ndc.x * 0.5 + 0.5,
		0.5 - ndc.y * 0.5);
}

float3 ReconstructPositionFromRawDepth(
	float2 uv,
	float rawDepth,
	float4x4 inverseTransform)
{
	const float2 ndc = UVToNDC(uv);
	const float4 homogeneousPosition = mul(
		float4(ndc, rawDepth, 1.0),
		inverseTransform);
	return homogeneousPosition.xyz / homogeneousPosition.w;
}
