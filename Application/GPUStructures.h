#pragma once
#include <SimpleMath.h>
using namespace DirectX::SimpleMath;
// struct member name without m_ for GPU using

namespace gglab
{
#define GGLAB_GPU_STRUCTURE_ALIGNAS_VALUE (16)
#define GGLAB_GPU_STRUCTURE_ALIGNAS alignas(GGLAB_GPU_STRUCTURE_ALIGNAS_VALUE)

	struct GGLAB_GPU_STRUCTURE_ALIGNAS LightData
	{
		Vector4 Position;
		Vector4 Direction;
		Vector4 Color;
		float Intensity;
		float Range;
		float SpotAngle;
		uint32_t LightType;
	};

	struct GGLAB_GPU_STRUCTURE_ALIGNAS FrameCBData
	{
		Matrix ViewMat;
		Matrix ProjMat;
		Vector4 CameraPos;
		float Exposure;
		
		uint32_t ObjectBaseIndex;
		uint32_t MaterialBaseIndex;
		uint32_t Padding;

		LightData MainLight;
	};

	struct GGLAB_GPU_STRUCTURE_ALIGNAS ObjectCBData
	{
		uint32_t ObjectIndex;
		uint32_t Padding[3];
	};

	struct GGLAB_GPU_STRUCTURE_ALIGNAS MaterialCBData
	{
		uint32_t MaterialIndex;
		uint32_t Padding[3];
	};

	struct GGLAB_GPU_STRUCTURE_ALIGNAS ObjectGPU
	{
		Matrix ModelMat;
		Matrix NormalMat;
		uint32_t MaterialIndex;
		uint32_t Padding[3];
	};
	static constexpr uint32_t MaxObjectCapacity = 1024;


	struct GGLAB_GPU_STRUCTURE_ALIGNAS MaterialGPU
	{
		Vector4 BaseColorFactor;
		float MetallicFactor;
		float RoughnessFactor;
		float NormalScale;
		float OcclusionStrength;
		Vector4 EmissiveColorFactor;
	};
	static constexpr uint32_t MaxMaterialCapacity = 256;
}