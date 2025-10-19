#pragma once
#include <SimpleMath.h>
using namespace DirectX::SimpleMath;
// struct member name without m_ for GPU using

namespace gglab
{
	struct alignas(16) GlobalCBData
	{
		Matrix ViewMat;
		Matrix ProjMat;
		Vector4 CameraPos;
		Vector4 MainLightDir;
		Vector3 MainLightColor;
		float Exposure;
	};

	struct alignas(16) ObjectCBData
	{
		Matrix ModelMat;
	};

	struct alignas(16) MaterialCBData
	{
		Vector4 BaseColor;
		float Metallic;
		float Roughness;
		float NormalScale;
		float OcclusionStrength;
		Vector4 EmissiveColor;
	};

	struct alignas(16) LightData
	{
		Vector4 Position;
		Vector4 Direction;
		Vector4 Color;
		uint8_t LightType;
	};
}