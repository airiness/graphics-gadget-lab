#pragma once

#include "GGLabRuntime/Scene/DirectionalLightTooling.h"

namespace gglab
{
	class World;

	class DirectionalLightTooling final : public DirectionalLightViewBase, public DirectionalLightControlBase
	{
	public:
		explicit DirectionalLightTooling(World& world) noexcept : m_World(world) {}
		[[nodiscard]] std::optional<DirectionalLightObservation> GetLight() const noexcept override;
		void SetDirection(uint32_t id, const Vector3& direction) noexcept override;
		void SetRadiance(uint32_t id, const Color& color, float intensity) noexcept override;
		void SetShadowSettings(uint32_t id,
			const std::optional<DirectionalShadowSettings>& settings) noexcept override;

	private:
		World& m_World;
	};
}
