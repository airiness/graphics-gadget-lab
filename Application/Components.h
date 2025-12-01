#pragma once
#include "GraphicsTypes.h"
#include "Color.h"
#include "StringId.h"

namespace gglab
{
	namespace components
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
			Color m_Color;
			float Intensity;
			float Range;
			float SpotAngle;
		};
	}
}