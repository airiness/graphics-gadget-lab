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

struct FrameCBData
{
	float4x4 ViewMat;
	float4x4 ProjMat;
	float4 CameraPosWS;
	float Exposure;

	uint ObjectBaseIndex;
	uint MaterialBaseIndex;
	uint Padding; //uint g_TextureBaseIndex; // TODO: bindless textures
	
	LightCBData MainLight;
};

struct ObjectData
{
	float4x4 ModelMat;
	float4x4 NormalMat;
	uint MaterialIndex;
	uint3 Padding;
};

struct MaterialData
{
	float4 BaseColorFactor;
	float MetallicFactor;
	float RoughnessFactor;
	float NormalScale;
	float OcclusionStrength;
	float4 EmissiveColorFactor;
};