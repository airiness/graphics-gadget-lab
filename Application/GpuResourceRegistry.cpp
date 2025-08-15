#include "Precompiled.h"
#include "GpuResourceRegistry.h"
#include "DX12Texture.h"
#include "DX12Buffer.h"

namespace graphicsGadgetLab
{
	GpuResourceRegistry::GpuResourceRegistry(DX12Device* device) noexcept :
		m_DX12Device(device)
	{
	}

	void GpuResourceRegistry::ReleaseAll() noexcept
	{
		// TODO: Release Gpu resources safety.
		m_Textures.clear();
		m_Buffers.clear();
	}

	DX12Texture* GpuResourceRegistry::GetTexture(int32_t index) const noexcept
	{
		GGLAB_ASSERT_MSG(index >= 0 && index < m_Textures.size(), "Invalid Texture index.");

		return m_Textures[index].get();
	}

	DX12Buffer* GpuResourceRegistry::GetBuffer(int32_t index) const noexcept
	{
		GGLAB_ASSERT_MSG(index >= 0 && index < m_Buffers.size(), "Invalid Buffer index.");

		return m_Buffers[index].get();
	}
}