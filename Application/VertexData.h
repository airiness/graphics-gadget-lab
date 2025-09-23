#pragma once
#include "SimpleMath.h"
using namespace DirectX::SimpleMath;

namespace gglab
{
	struct Vertex
	{
		Vector3 m_Position = Vector3::Zero;
		Vector3 m_Normal = Vector3::UnitX;
		Vector2 m_TexCoord = Vector2::Zero;
	};
}