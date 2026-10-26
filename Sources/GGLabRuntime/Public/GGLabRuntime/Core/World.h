#pragma once
#include "GGLabFoundation/Base/CoreMacros.h"

#include <entt/entity/registry.hpp>
#include "GGLabRuntime/Graphics/Atmosphere.h"
#include <optional>
#include <algorithm>
#include <cstdint>
#include <limits>
#include <utility>

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
			m_LightingControlRevision = other.m_LightingControlRevision;
			other.MarkLightingAuthoringChanged();
		}
		World& operator=(World&& other) noexcept
		{
			if (this != &other)
			{
				m_Registry = std::move(other.m_Registry);
				m_Atmosphere = std::move(other.m_Atmosphere);
				m_LightingControlRevision = std::max(m_LightingControlRevision, other.m_LightingControlRevision);
				MarkLightingAuthoringChanged();
				other.MarkLightingAuthoringChanged();
			}
			return *this;
		}
		~World() = default;

		entt::registry& GetRegistry() noexcept { return m_Registry; }
		const entt::registry& GetRegistry() const noexcept { return m_Registry; }
		// Live authoring writers mark sun/atmosphere edits, including tooling and
		// raw ECS edits. This is independent of Runtime bake/publication generations.
		void MarkLightingAuthoringChanged() noexcept
		{
			if (m_LightingControlRevision != std::numeric_limits<uint64_t>::max())
			{
				++m_LightingControlRevision;
			}
		}
		[[nodiscard]] uint64_t GetLightingControlRevision() const noexcept { return m_LightingControlRevision; }

		std::optional<AtmosphereSettings> m_Atmosphere;
	private:
		entt::registry m_Registry;
		uint64_t m_LightingControlRevision = 1;
	};
}
