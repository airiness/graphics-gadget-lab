#include "GGLabRuntime/Graphics/RenderGraph/RGBlackboard.h"

namespace gglab
{
	RGBlackboard::RGBlackboard(RGObjectArena& objectArena) noexcept :
		m_ObjectArena(objectArena)
	{
	}

	void RGBlackboard::ClearIndex() noexcept
	{
		m_Container.clear();
	}
}
