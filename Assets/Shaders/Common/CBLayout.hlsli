#pragma once

struct LightCBData
{
	float4 PosWS;
	float4 DirWS;
	float4 Color;
	float Intensity;
	float Range;
	float SpotAngle;
	uint LightType; // 0: Directional, 1: Point, 2: Spot
};

struct GlobalCBData
{
	float4x4 ViewMat;
	float4x4 ProjMat;
	float4 CameraPosWS;
	LightCBData MainLight;
	float Exposure;
	float3 Padding;
};

struct ObjectCBData
{
	float4x4 ModelMat;
	float4x4 NormalMat;
};

struct MaterialCBData
{
	float4 BaseColorFactor;
	float MetallicFactor;
	float RoughnessFactor;
	float NormalScale;
	float OcclusionStrength;
	float4 EmissiveColorFactor;
};