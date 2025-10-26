#pragma once
#include <SimpleMath.h>
using namespace DirectX::SimpleMath;
// struct member name without m_ for GPU using

namespace gglab
{
#define GGLAB_GPU_STRUCTURE_ALIGNAS alignas(16)

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

	struct GGLAB_GPU_STRUCTURE_ALIGNAS GlobalCBData
	{
		Matrix ViewMat;
		Matrix ProjMat;
		Vector4 CameraPos;
		LightData MainLight;
		float Exposure;
		Vector3 Padding;
	};

	struct GGLAB_GPU_STRUCTURE_ALIGNAS ObjectCBData
	{
		Matrix ModelMat;
		Matrix NormalMat;
	};

	struct GGLAB_GPU_STRUCTURE_ALIGNAS MaterialCBData
	{
		Vector4 BaseColorFactor;
		float MetallicFactor;
		float RoughnessFactor;
		float NormalScale;
		float OcclusionStrength;
		Vector4 EmissiveColorFactor;
	};
}