#pragma once
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

	struct FrameCBData
	{
		Matrix ViewMat;
		Matrix ProjMat;
		Vector4 CameraPos;
		float Exposure;
		
		uint32_t ObjectBaseIndex;
		uint32_t MaterialBaseIndex;
		uint32_t Padding;

		LightGPU MainLight;
	};

	struct ObjectGPU
	{
		Matrix ModelMat;
		Matrix NormalMat;
		uint32_t MaterialIndex;
		uint32_t Padding[3];
	};
	static constexpr uint32_t MaxObjectCapacity = 1024;


	struct MaterialGPU
	{
		Color BaseColorFactor;
		float MetallicFactor;
		float RoughnessFactor;
		float NormalScale;
		float OcclusionStrength;
		Color EmissiveColorFactor;

		uint32_t BaseColorTexIndex;
		uint32_t MetallicRoughnessTexIndex;
		uint32_t NormalTexIndex;
		uint32_t OcclusionTexIndex;
		uint32_t EmissiveTexIndex;
	};
	static constexpr uint32_t MaxMaterialCapacity = 128;
}