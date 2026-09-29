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
	float EnvironmentRotationRadians;
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
	uint LightBaseIndex;
	uint LightCount;
	uint DirectionalShadowLightIndex;
	uint WorldSunLightIndex;
	float WorldSunAngularRadius;
	uint Padding;

	IBLResourceData IBLResource;
	float4 AtmosphereWorld;
	float4 AtmosphereRadii;
};

struct ObjectData
{
	matrix ModelMat;
	matrix PreviousModelMat;
	matrix NormalMat;
	uint MaterialIndex;
	uint ViewIndex;
	uint2 Padding;
};

struct MaterialTextureBindingData
{
	TextureSamplerBindingData TextureSamplerBinding;
	uint TexCoordIndex;
	uint TextureEnabled;
	float4 UVTransformU;
	float4 UVTransformV;
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
	uint Flags;		// bit 0: doubleSided
	uint DebugView; // Matches MaterialDebugView in MaterialTypes.h.
	float Ior;
	float ClearcoatFactor;
	float ClearcoatRoughness;
	float ClearcoatNormalScale;
	MaterialTextureBindingData ClearcoatBinding;
	MaterialTextureBindingData ClearcoatRoughnessBinding;
	MaterialTextureBindingData ClearcoatNormalBinding;
	float AnisotropyStrength;
	float AnisotropyRotation;
	uint AnisotropyTextureEnabled;
	uint AnisotropyPadding;
	MaterialTextureBindingData AnisotropyBinding;
	float4 SheenColorFactor;
	float SheenRoughnessFactor;
	float3 SheenPadding;
	MaterialTextureBindingData SheenColorBinding;
	MaterialTextureBindingData SheenRoughnessBinding;
};

struct ViewData
{
	matrix ViewMat;
	matrix ProjMat;
	matrix InvViewMat;
	matrix InvProjMat;
	matrix PreviousViewMat;
	matrix PreviousRasterViewProj;
	float4 CameraPos;
	float4 PreviousDepthReconstructionParams;
	float Near;
	float Far;
	float FovRadians;
	float Aspect;
	float2 CurrentJitterUV;
	float2 PreviousJitterUV;
	float ExposureMultiplier;
	uint Width;
	uint Height;
	uint DepthConvention;
	uint PreviousDepthConvention;
	float ScenePreExposure;
	float PreviousScenePreExposure;
	uint Padding;
};
