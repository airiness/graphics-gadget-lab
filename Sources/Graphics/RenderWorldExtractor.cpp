#include "Core/Precompiled.h"
#include "Graphics/RenderWorldExtractor.h"
#include "Core/Components.h"
#include "Core/World.h"
#include "Graphics/GraphicsTypes.h"

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
		auto& registry = world.GetRegistry();
		auto lightView = registry.view<components::TransformComponent, components::LightComponent>();
		for (auto [entity, transform, light] : lightView.each())
		{
			GGLAB_UNUSED(entity);
			if (light.m_Type != LightType::Directional)
			{
				continue;
			}

			RenderDirectionalLight directionalLight{};
			directionalLight.m_EntityKey = static_cast<uint64_t>(entt::to_integral(entity));
			directionalLight.m_Transform = &transform;
			directionalLight.m_Light = &light;
			directionalLight.m_Direction = ResolveLightDirection(transform);
			if (light.m_DirectionalShadowSettings)
			{
				directionalLight.m_ShadowSettings = &*light.m_DirectionalShadowSettings;
			}
			return directionalLight;
		}

		return {};
	}

	Vector3 RenderWorldExtractor::ResolveLightDirection(const components::TransformComponent& transform) noexcept
	{
		Matrix rotation = Matrix::CreateFromQuaternion(transform.m_Rotation);
		Vector3 forward = Vector3::Transform(-Vector3::UnitZ, rotation);
		if (forward.LengthSquared() <= 1.0e-8f)
		{
			return -Vector3::UnitY;
		}

		forward.Normalize();
		return forward;
	}
}
