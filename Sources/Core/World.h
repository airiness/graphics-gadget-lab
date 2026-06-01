#pragma once

namespace gglab
{
	class World
	{
	public:
		World() noexcept = default;
		GGLAB_DELETE_COPYABLE_DEFAULT_MOVABLE(World);
		~World() = default;

		entt::registry& GetRegistry() noexcept { return m_Registry; }
		const entt::registry& GetRegistry() const noexcept { return m_Registry; }

	private:
		entt::registry m_Registry;
	};
}