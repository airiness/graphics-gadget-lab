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
	uint LightType; // 0: Directional, 1: Spot, 2: Point
};

struct TextureSamplerBindingData
{
	uint TextureIndex;
	uint SamplerIndex;
};

struct IBLResourceData
{
	// Nested struct is aligned to 16bytes in ConstantBuffer, use uint2 replace struct TextureSamplerBindingData
	uint2 EnvironmentBinding;
	uint2 IrradianceBinding;
	uint2 PrefilteredSpecularBinding;
	uint2 BrdfLutBinding;

	uint PrefilteredSpecularMipLevels;
	float EnvironmentIntensity;
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
	uint LightBaseIndex;
	uint LightCount;
	
	IBLResourceData IBLResource;
};

struct ObjectData
{
	matrix ModelMat;
	matrix NormalMat;
	uint MaterialIndex;
	uint ViewIndex;
	uint2 Padding;
};

struct MaterialTextureBindingData
{
	TextureSamplerBindingData TextureSamplerBinding;
	uint TexCoordIndex;
	uint Padding;
};

struct MaterialData
{
	MaterialTextureBindingData BaseColorBinding;
	MaterialTextureBindingData EmissiveBinding;	
	MaterialTextureBindingData MetallicRoughnessBinding;
	MaterialTextureBindingData NormalBinding;
	MaterialTextureBindingData OcclusionBinding;
	
	float4 BaseColorFactor;
	float4 EmissiveColorFactor;
	
	float MetallicFactor;
	float RoughnessFactor;
	float NormalScale;
	float OcclusionStrength;
	
	int AlphaMode; // 0: OPAQUE, 1: MASK, 2: BLEND. Defined in MaterialUtils.hlsli
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
