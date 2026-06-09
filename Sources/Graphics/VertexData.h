#pragma once
#include "Core/Math/MathTypes.h"

namespace gglab
{
	struct Vertex
	{
		Vector3 m_Position = Vector3::Zero;
		Vector3 m_Normal = Vector3::UnitX;
		Vector2 m_TexCoord0 = Vector2::Zero;
		Vector2 m_TexCoord1 = Vector2::Zero;
		Vector4 m_Tangent = Vector4(0.0f, 0.0f, 1.0f, 1.0f);
	};
}
