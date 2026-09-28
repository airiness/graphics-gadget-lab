#include "Graphics/RenderWorldExtractor.h"
#include "GGLabFoundation/Base/CoreMacros.h"
#include "GGLabRuntime/Core/Math/Matrix.h"
#include "GGLabRuntime/Core/World.h"
#include "GGLabRuntime/Graphics/GraphicsTypes.h"
#include "GGLabRuntime/Scene/Components.h"

#include <cmath>

namespace gglab
{
	RenderWorldData RenderWorldExtractor::Extract(World& world) const noexcept
	{
		RenderWorldData data{};
		data.m_MainDirectionalLight = ExtractMainDirectionalLight(world);
		return data;
	}

	RenderDirectionalLight RenderWorldExtractor::ExtractMainDirectionalLight(World& world) noexcept
	{
		RenderDirectionalLight selected{};
		auto& registry = world.GetRegistry();
		auto lightView =
			registry.view<components::TransformComponent, components::LightComponent>();
		for (auto [entity, transform, light] : lightView.each())
		{
			if (light.m_Type != LightType::Directional)
			{
				continue;
			}

			// Explicit suns take priority. Multiple designations select the lowest entity
			// identity deterministically; authoring tools enforce a single designation.
			const uint64_t key = static_cast<uint64_t>(entt::to_integral(entity));
			if (selected.m_EntityKey &&
				(selected.m_WorldSun.has_value() > light.m_WorldSun.has_value() ||
				(selected.m_WorldSun.has_value() == light.m_WorldSun.has_value() &&
					(!light.m_WorldSun || *selected.m_EntityKey < key))))
			{
				continue;
			}
			RenderDirectionalLight directionalLight{};
			directionalLight.m_EntityKey = static_cast<uint64_t>(entt::to_integral(entity));
			directionalLight.m_Transform = &transform;
			directionalLight.m_Light = &light;
			directionalLight.m_Direction = ResolveLightDirection(transform);
			if (light.m_WorldSun)
			{
				directionalLight.m_WorldSun = ResolveWorldSun(*light.m_WorldSun, directionalLight.m_Direction);
				directionalLight.m_Direction = directionalLight.m_WorldSun->m_Direction;
			}
			if (light.m_DirectionalShadowSettings)
			{
				directionalLight.m_ShadowSettings = &*light.m_DirectionalShadowSettings;
			}
			selected = directionalLight;
		}

		return selected;
	}

	Vector3 RenderWorldExtractor::ResolveLightDirection(
		const components::TransformComponent& transform) noexcept
	{
		const Matrix rotation = math::CreateFromQuaternion(transform.m_Rotation);
		Vector3 forward = math::TransformDirection(Vector3::Forward, rotation);
		if (!std::isfinite(forward.LengthSquared()) || forward.LengthSquared() <= 1.0e-8f)
		{
			return -Vector3::UnitY;
		}

		forward.Normalize();
		return forward;
	}
}
