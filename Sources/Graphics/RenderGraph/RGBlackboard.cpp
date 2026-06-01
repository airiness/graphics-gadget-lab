#include "Core/Precompiled.h"
#include "Graphics/RenderGraph/RGBlackboard.h"

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