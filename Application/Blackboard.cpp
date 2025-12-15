#include "Precompiled.h"
#include "Blackboard.h"

namespace gglab
{
	Blackboard::Blackboard(RGArenaAllocator& arenaAllocator) noexcept :
		m_ArenaAllocator(arenaAllocator)
	{
	}

	void Blackboard::Reset() noexcept
	{
		m_Container.clear();
	}
}