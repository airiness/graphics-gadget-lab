#pragma once
#include "Core/Math/MathTypes.h"
#include "Graphics/ShadowSettings.h"

namespace gglab
{
	class World;

	namespace components
	{
		struct LightComponent;
		struct TransformComponent;
	}

	struct RenderDirectionalLight
	{
		std::optional<uint64_t> m_EntityKey;
		components::TransformComponent* m_Transform = nullptr;
		components::LightComponent* m_Light = nullptr;
		DirectionalShadowSettings* m_ShadowSettings = nullptr;
		Vector3 m_Direction = -Vector3::UnitY;
	};

	struct RenderWorldData
	{
		RenderDirectionalLight m_MainDirectionalLight{};

		const DirectionalShadowSettings& GetMainDirectionalShadowSettings() const noexcept
		{
			return m_MainDirectionalLight.m_ShadowSettings ?
				*m_MainDirectionalLight.m_ShadowSettings :
				DisabledDirectionalShadowSettings();
		}
	};

	class RenderWorldExtractor
	{
	public:
		RenderWorldData Extract(World& world) const noexcept;

	private:
		static RenderDirectionalLight ExtractMainDirectionalLight(World& world) noexcept;
		static Vector3 ResolveLightDirection(const components::TransformComponent& transform) noexcept;
	};
}
