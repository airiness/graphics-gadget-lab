#pragma once
#include "Graphics/GraphicsTypes.h"
#include "Core/Math/Color.h"
#include "Core/Math/Quaternion.h"
#include "Core/Math/Vector.h"
#include "Graphics/ShadowSettings.h"

#include <optional>

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
		ModelID m_ModelId{};
	};

	struct MaterialInstanceComponent
	{
		RuntimeMaterialKey m_Key{};
		MaterialProperties m_Properties{};
	};

	struct LightComponent
	{
		LightType m_Type = LightType::Directional;
		Color m_Color = Color::White;
		float m_Intensity = 1.0f;
		float m_Range = 1000.0f;
		float m_SpotAngle = 60.0f;
		std::optional<DirectionalShadowSettings> m_DirectionalShadowSettings;
	};
}
