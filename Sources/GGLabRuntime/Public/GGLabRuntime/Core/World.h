#pragma once
#include "GGLabFoundation/Base/CoreMacros.h"

#include <entt/entity/registry.hpp>
#include "GGLabRuntime/Graphics/Atmosphere.h"
#include <optional>

namespace gglab
{
	class World
	{
	public:
		World() noexcept = default;
		GGLAB_DELETE_COPYABLE(World);
		World(World&& other) noexcept
		{
			// Leave a usable empty registry so borrowed tooling can reject moved-away targets.
			m_Registry.swap(other.m_Registry);
			m_Atmosphere.swap(other.m_Atmosphere);
		}
		World& operator=(World&&) noexcept = default;
		~World() = default;

		entt::registry& GetRegistry() noexcept { return m_Registry; }
		const entt::registry& GetRegistry() const noexcept { return m_Registry; }

		std::optional<AtmosphereSettings> m_Atmosphere;
	private:
		entt::registry m_Registry;
	};
}
