#pragma once

// Matches CubemapFace in Graphics/Utility/CubemapUtils.h and the D3D TextureCube array-slice order.
static const uint CUBEMAP_FACE_POSITIVE_X = 0;
static const uint CUBEMAP_FACE_NEGATIVE_X = 1;
static const uint CUBEMAP_FACE_POSITIVE_Y = 2;
static const uint CUBEMAP_FACE_NEGATIVE_Y = 3;
static const uint CUBEMAP_FACE_POSITIVE_Z = 4;
static const uint CUBEMAP_FACE_NEGATIVE_Z = 5;
static const uint CUBEMAP_FACE_COUNT = 6;

float3 GetCubemapFaceDirection(uint face)
{
	switch (face)
	{
		case CUBEMAP_FACE_POSITIVE_X:
			return float3(1.0, 0.0, 0.0);
		case CUBEMAP_FACE_NEGATIVE_X:
			return float3(-1.0, 0.0, 0.0);
		case CUBEMAP_FACE_POSITIVE_Y:
			return float3(0.0, 1.0, 0.0);
		case CUBEMAP_FACE_NEGATIVE_Y:
			return float3(0.0, -1.0, 0.0);
		case CUBEMAP_FACE_POSITIVE_Z:
			return float3(0.0, 0.0, 1.0);
		case CUBEMAP_FACE_NEGATIVE_Z:
			return float3(0.0, 0.0, -1.0);
		default:
			return float3(0.0, 0.0, 1.0);
	}
}

float3 GetCubemapFaceUp(uint face)
{
	switch (face)
	{
		case CUBEMAP_FACE_POSITIVE_Y:
			return float3(0.0, 0.0, -1.0);
		case CUBEMAP_FACE_NEGATIVE_Y:
			return float3(0.0, 0.0, 1.0);
		default:
			return float3(0.0, 1.0, 0.0);
	}
}

float3 GetCubemapFaceRight(uint face)
{
	const float3 direction = GetCubemapFaceDirection(face);
	const float3 up = GetCubemapFaceUp(face);
	return normalize(cross(up, direction));
}

float3 CubemapFaceUvToDirection(uint face, float2 uv)
{
	const float2 ndc = uv * 2.0 - 1.0;
	const float3 direction = GetCubemapFaceDirection(face);
	const float3 right = GetCubemapFaceRight(face);
	const float3 up = GetCubemapFaceUp(face);
	return normalize(direction + ndc.x * right - ndc.y * up);
}
