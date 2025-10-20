#pragma once
#include <SimpleMath.h>
using namespace DirectX::SimpleMath;
// struct member name without m_ for GPU using

namespace gglab
{
#define GGLAB_GPU_STRUCTURE_ALIGNAS alignas(16)

	struct GGLAB_GPU_STRUCTURE_ALIGNAS GlobalCBData
	{
		Matrix ViewMat;
		Matrix ProjMat;
		Vector4 CameraPos;
		Vector4 MainLightDir;
		Vector3 MainLightColor;
		float Exposure;
	};

	struct GGLAB_GPU_STRUCTURE_ALIGNAS ObjectCBData
	{
		Matrix ModelMat;
	};

	struct GGLAB_GPU_STRUCTURE_ALIGNAS MaterialCBData
	{
		Vector4 BaseColor;
		float Metallic;
		float Roughness;
		float NormalScale;
		float OcclusionStrength;
		Vector4 EmissiveColor;
	};

	struct GGLAB_GPU_STRUCTURE_ALIGNAS LightData
	{
		Vector4 Position;
		Vector4 Direction;
		Vector4 Color;
		uint8_t LightType;
	};
}