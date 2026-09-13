#include "Diagnostics/DirectionalLightTooling.h"
#include "GGLabRuntime/Core/World.h"
#include "GGLabRuntime/Core/Math/Matrix.h"
#include "GGLabRuntime/Core/Math/Quaternion.h"
#include "GGLabRuntime/Scene/Components.h"

#include <cmath>

namespace gglab
{
	namespace
	{
		bool IsDirectionalLight(entt::registry& registry, entt::entity entity) noexcept
		{
			if (!registry.valid(entity) ||
				!registry.all_of<components::TransformComponent, components::LightComponent>(entity))
			{
				return false;
			}
			return registry.get<components::LightComponent>(entity).m_Type == LightType::Directional;
		}
	}

	std::optional<DirectionalLightObservation> DirectionalLightTooling::GetLight() const noexcept
	{
		const auto& registry = m_World.GetRegistry();
		for (auto [entity, transform, light] :
			registry.view<const components::TransformComponent, const components::LightComponent>().each())
		{
			if (light.m_Type != LightType::Directional)
			{
				continue;
			}
			Vector3 direction = math::TransformDirection(
				Vector3::Forward, math::CreateFromQuaternion(transform.m_Rotation));
			if (direction.LengthSquared() <= 1.0e-8f)
			{
				direction = -Vector3::UnitY;
			}
			else
			{
				direction.Normalize();
			}
			return DirectionalLightObservation{ entt::to_integral(entity), direction,
				light.m_Color, light.m_Intensity, light.m_DirectionalShadowSettings };
		}
		return std::nullopt;
	}

	void DirectionalLightTooling::SetDirection(uint32_t id, const Vector3& direction) noexcept
	{
		auto& registry = m_World.GetRegistry();
		const auto entity = static_cast<entt::entity>(id);
		const float lengthSquared = direction.LengthSquared();
		if (!IsDirectionalLight(registry, entity) || !std::isfinite(lengthSquared) || lengthSquared <= 1.0e-8f)
		{
			return;
		}
		Vector3 normalized = direction;
		normalized.Normalize();
		registry.get<components::TransformComponent>(entity).m_Rotation =
			math::RotationFromTo(Vector3::Forward, normalized);
	}

	void DirectionalLightTooling::SetRadiance(uint32_t id, const Color& color, float intensity) noexcept
	{
		auto& registry = m_World.GetRegistry();
		const auto entity = static_cast<entt::entity>(id);
		if (IsDirectionalLight(registry, entity))
		{
			auto& light = registry.get<components::LightComponent>(entity);
			light.m_Color = color;
			light.m_Intensity = intensity;
		}
	}

	void DirectionalLightTooling::SetShadowSettings(uint32_t id,
		const std::optional<DirectionalShadowSettings>& settings) noexcept
	{
		auto& registry = m_World.GetRegistry();
		const auto entity = static_cast<entt::entity>(id);
		if (IsDirectionalLight(registry, entity))
		{
			registry.get<components::LightComponent>(entity).m_DirectionalShadowSettings = settings;
		}
	}
}
