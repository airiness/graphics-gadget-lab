#include "Precompiled.h"
#include "RGBlackboard.h"

namespace gglab
{
	RGBlackboard::RGBlackboard(RGArenaAllocator& arenaAllocator) noexcept :
		m_ArenaAllocator(arenaAllocator)
	{
	}

	void RGBlackboard::Reset() noexcept
	{
		m_Container.clear();
	}
}