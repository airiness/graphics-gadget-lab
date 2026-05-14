#pragma once
#pragma pack_matrix(row_major) // Use row-major matrices

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

struct IBLResourceData
{
	uint BrdfLutTexIndex;
	uint BrdfLutSamplerIndex;
	
	uint2 Padding;
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
	
	IBLResourceData IBLResource;
	
	LightData MainLight;
};

struct ObjectData
{
	matrix ModelMat;
	matrix NormalMat;
	uint MaterialIndex;
	uint3 Padding;
};

struct TextureBindingData
{
	uint TextureIndex;
	uint SamplerIndex;
	uint TexCoordIndex;
	uint Padding;
};

struct MaterialData
{
	TextureBindingData BaseColorBinding;
	TextureBindingData EmissiveBinding;	
	TextureBindingData MetallicRoughnessBinding;
	TextureBindingData NormalBinding;
	TextureBindingData OcclusionBinding;
	
	float4 BaseColorFactor;
	float4 EmissiveColorFactor;
	
	float MetallicFactor;
	float RoughnessFactor;
	float NormalScale;
	float OcclusionStrength;
	
	int AlphaMode; // 0: OPAQUE, 1: MASK, 2: BLEND
	float AlphaCutoff;
	uint Flags; // bit 0: doubleSided
	
	uint Padding;
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