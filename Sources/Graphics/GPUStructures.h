#pragma once
#include "Core/Math/MathTypes.h"
#include "Core/TypedIndex.h"
#include "Graphics/GraphicsTypes.h"
// struct member name without m_ for GPU using

namespace gglab
{
#define GGLAB_GPU_STRUCTURE_ALIGNAS_VALUE (16)
#define GGLAB_GPU_STRUCTURE_ALIGNAS alignas(GGLAB_GPU_STRUCTURE_ALIGNAS_VALUE)

	struct LightGPU
	{
		Vector4 Position;
		Vector4 Direction;
		Vector4 Color;
		float Intensity;
		float Range;
		float SpotAngle;
		uint32_t LightType;
	};

	struct TextureSamplerBindingGPU
	{
		uint32_t TextureIndex;
		uint32_t SamplerIndex;
	};

	struct IBLResourceGPU
	{
		TextureSamplerBindingGPU EnvironmentBinding;
		TextureSamplerBindingGPU IrradianceBinding;
		TextureSamplerBindingGPU PrefilteredSpecularBinding;
		TextureSamplerBindingGPU BrdfLutBinding;

		uint32_t PrefilteredSpecularMipLevels;
		float EnvironmentIntensity;
		uint32_t Padding[2];
	};

	struct SceneGPU
	{
		uint32_t ObjectBaseIndex;
		uint32_t ObjectCount;
		uint32_t MaterialBaseIndex;
		uint32_t MaterialCount;
		uint32_t ViewBaseIndex;
		uint32_t ViewCount;
		uint32_t LightBaseIndex;
		uint32_t LightCount;

		IBLResourceGPU IBLResource;
	};

	struct ObjectGPU
	{
		Matrix ModelMat;
		Matrix NormalMat;
		uint32_t MaterialIndex;
		uint32_t ViewIndex;
		uint32_t Padding[2];
	};
	static constexpr uint32_t MaxObjectCapacity = 1024;

	struct MaterialTextureBindingGPU
	{
		TextureSamplerBindingGPU TextureSamplerBinding;
		uint32_t TexCoordIndex;
		uint32_t Padding;
	};

	struct MaterialGPU
	{
		MaterialTextureBindingGPU BaseColorBinding;
		MaterialTextureBindingGPU EmissiveBinding;
		MaterialTextureBindingGPU MetallicRoughnessBinding;
		MaterialTextureBindingGPU NormalBinding;
		MaterialTextureBindingGPU OcclusionBinding;

		Color BaseColorFactor;
		Color EmissiveColorFactor;

		float MetallicFactor;
		float RoughnessFactor;
		float NormalScale;
		float OcclusionStrength;

		int32_t AlphaMode; // 0: OPAQUE, 1: MASK, 2: BLEND
		float AlphaCutoff;
		uint32_t Flags; // bit 0: doubleSided

		uint32_t Padding;
	};
	static constexpr uint32_t MaxMaterialCapacity = 128;
	static constexpr uint32_t MaxLightCapacity = 64;

	struct ViewGPU
	{
		Matrix ViewMat;
		Matrix ProjMat;
		Matrix InvViewMat;
		Matrix InvProjMat;
		Vector4 CameraPos;
		float Near;
		float Far;
		float FovRadians;
		float Aspect;
		float Exposure;
		uint32_t Width;
		uint32_t Height;
		uint32_t Padding;
	};
	static constexpr uint32_t MaxViewCapacity = 
		static_cast<uint32_t>(utils::ToIndex(RenderViewID::Count)) * 8;
}
