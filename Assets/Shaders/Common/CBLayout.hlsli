#pragma once

struct FrameCBData
{
	float4x4 ViewMat;
	float4x4 ProjMat;
	float4 CameraPosWS;
	float4 MainLightDir;
	float3 MainLightColor;
	float Exposure;
};

struct ObjectCBData
{
	float4x4 ModelMat;
};

struct MaterialCBData
{
	float4 BaseColor;
	float Metallic;
	float Roughness;
	float NormalScale;
	float OcclusionStrength;
	float4 EmissiveColor;
};