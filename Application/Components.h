#pragma once
#include "GraphicsTypes.h"
#include "Color.h"

namespace gglab::components
{
	struct TransformComponent
	{
		Vector3 m_Position = Vector3::Zero;
		Quaternion m_Rotation = Quaternion::Identity;
		Vector3 m_Scale = Vector3::One;
	};

	struct ModelComponent
	{
		ModelId m_ModelId{};
	};

	struct LightComponent
	{
		LightType m_Type = LightType::Directional;
		Color m_Color = color::White;
		float m_Intensity = 1.0f;
		float m_Range = 1000.0f;
		float m_SpotAngle = 60.0f;
	};
}