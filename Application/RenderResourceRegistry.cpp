#include "Precompiled.h"
#include "RenderResourceRegistry.h"

namespace gglab
{
	RenderResourceRegistry::RenderResourceRegistry(const CreateInfo& createInfo) noexcept :
		m_GpuResourceAllocator(createInfo.m_GpuResourceAllocator),
		m_ExternalResourceRegistry(createInfo.m_ExternalResourceRegistry)
	{
		GGLAB_ASSERT(m_GpuResourceAllocator);
		GGLAB_ASSERT(m_ExternalResourceRegistry);
	}

	DX12Texture* RenderResourceRegistry::GetTexture(TextureIndex index) noexcept
	{
		return nullptr;
	}
}
