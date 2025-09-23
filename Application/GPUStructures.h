#pragma once
#include <SimpleMath.h>
using namespace DirectX::SimpleMath;
// struct member name without m_ for GPU using

namespace gglab
{
	struct GlobalConstantBuffer
	{
		Matrix m_ModelMatrix;
		Matrix m_ViewMatrix;
		Matrix m_ProjectionMatrix;
	};

	struct alignas(16) LightGPU
	{
		Vector4 position;
		Vector4 direction;
		Vector4 color;
		uint8_t lightType;
	};
}