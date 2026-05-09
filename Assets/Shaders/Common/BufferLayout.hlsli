#pragma once

struct LightData
{
	float4 Position;
	float4 Direction;
	float4 Color;
	float Intensity;
	float Range;
	float SpotAngle;
	uint LightType; // 0: Directional, 1: Point, 2: Spot
};

struct IBLResource
{
	uint BrdfLutIndex;
	uint IrradianceMapIndex;
	uint PrefilteredEnvMapIndex;
	uint Padding;
};

struct SceneData
{
	uint ObjectBaseIndex;
	uint ObjectCount;
	uint MaterialBaseIndex;
	uint MaterialCount;
	uint ViewBaseIndex;
	uint ViewCount;
	uint2 Padding;
	
	IBLResource IBLResource;
	
	LightData MainLight;
};

struct ObjectData
{
	matrix ModelMat;
	matrix NormalMat;
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
	
	uint BaseColorTexIndex;
	uint MetallicRoughnessTexIndex;
	uint NormalTexIndex;
	uint OcclusionTexIndex;
	uint EmissiveTexIndex;
	
	int AlphaMode; // 0: OPAQUE, 1: MASK, 2: BLEND
	float AlphaCutoff;
	uint Flags; // bit 0: doubleSided
};

struct ViewData
{
	matrix ViewMat;
	matrix ProjMat;
	matrix InvViewMat;
	matrix InvProjMat;
	float4 CameraPos;
	float Near;
	float Far;
	float FovRadians;
	float Aspect;
	float Exposure;
	uint Width;
	uint Height;
	uint Padding;
};