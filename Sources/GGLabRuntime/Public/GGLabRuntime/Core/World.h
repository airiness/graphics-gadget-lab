#pragma once
#include "GGLabFoundation/Base/CoreMacros.h"

#include <entt/entity/registry.hpp>

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
		}
		World& operator=(World&&) noexcept = default;
		~World() = default;

		entt::registry& GetRegistry() noexcept { return m_Registry; }
		const entt::registry& GetRegistry() const noexcept { return m_Registry; }

	private:
		entt::registry m_Registry;
	};
}
