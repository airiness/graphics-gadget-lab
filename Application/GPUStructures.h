#pragma once
#include "TypedIndex.h"
#include "GraphicsTypes.h"

#include <SimpleMath.h>
using namespace DirectX::SimpleMath;
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

	struct IBLResourceGPU
	{
		uint32_t BrdfLutTexIndex;
		uint32_t BrdfLutSamplerIndex;

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
		uint32_t Padding[2];

		IBLResourceGPU IBLResource;

		LightGPU MainLight; // TODO: move to structured buffer lights
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

	struct TextureBindingGPU
	{
		uint32_t TextureIndex;
		uint32_t SamplerIndex;
		uint32_t TexCoordIndex;
		uint32_t Padding;
	};

	struct MaterialGPU
	{
		TextureBindingGPU BaseColorBinding;
		TextureBindingGPU EmissiveBinding;
		TextureBindingGPU MetallicRoughnessBinding;
		TextureBindingGPU NormalBinding;
		TextureBindingGPU OcclusionBinding;

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