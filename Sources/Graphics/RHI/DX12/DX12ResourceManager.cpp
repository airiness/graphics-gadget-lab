#include "Core/Precompiled.h"
#include "Graphics/RHI/DX12/DX12ResourceManager.h"
#include "Graphics/RHI/DX12/DX12Buffer.h"
#include "Graphics/RHI/DX12/DX12Device.h"
#include "Graphics/RHI/DX12/DX12RHITypeUtils.h"
#include "Graphics/RHI/DX12/DX12Texture.h"

namespace gglab
{
	namespace
	{
		template<typename Handle>
		typename Handle::GenerationType NextHandleGeneration(typename Handle::GenerationType generation) noexcept
		{
			++generation;
			if (generation == Handle::InvalidGeneration)
			{
				++generation;
			}

			return generation;
		}
	}

	DX12ResourceManager::~DX12ResourceManager() noexcept = default;

	void DX12ResourceManager::Initialize(DX12Device* device) noexcept
	{
		GGLAB_ASSERT_MSG(device != nullptr, "DX12ResourceManager requires a valid DX12Device.");
		GGLAB_ASSERT_MSG(device->GetMemAllocator() != nullptr, "DX12ResourceManager requires an initialized memory allocator.");

		m_Device = device;
	}

	void DX12ResourceManager::Finalize() noexcept
	{
		m_TextureSlots.clear();
		m_FreeTextureSlots.clear();
		m_PendingDestroyedTextures.clear();

		m_BufferSlots.clear();
		m_FreeBufferSlots.clear();
		m_PendingDestroyedBuffers.clear();

		m_Device = nullptr;
	}

	RHITextureHandle DX12ResourceManager::CreateTexture(const RHITextureDesc& desc) noexcept
	{
		GGLAB_ASSERT_MSG(m_Device != nullptr, "DX12ResourceManager must be initialized before creating textures.");
		GGLAB_ASSERT_MSG(m_Device->GetMemAllocator() != nullptr, "DX12 memory allocator is not initialized.");

		auto texture = std::make_unique<DX12Texture>();

		DX12Resource::CreateInfo createInfo{};
		createInfo.m_Allocator = m_Device->GetMemAllocator();
		createInfo.m_AllocDesc.HeapType = D3D12_HEAP_TYPE_DEFAULT;
		createInfo.m_AllocDesc.Flags = D3D12MA::ALLOCATION_FLAG_NONE;
		createInfo.m_ResourceDesc = ToD3D12ResourceDesc(desc);
		createInfo.m_InitStates = D3D12_RESOURCE_STATE_COMMON;
		createInfo.m_ClearValue = ToD3D12ClearValue(desc.m_ClearValue);
		texture->Create(createInfo);

		const uint32_t slotIndex = AllocateTextureSlot();
		TextureSlot& slot = m_TextureSlots[slotIndex];
		GGLAB_ASSERT_MSG(!slot.m_Alive, "Allocated RHI texture slot is already alive.");
		slot.m_Ownership = DX12ResourceOwnership::Owned;
		slot.m_Alive = true;
		slot.m_Texture = std::move(texture);

		return RHITextureHandle(slotIndex, slot.m_Generation);
	}

	RHIBufferHandle DX12ResourceManager::CreateBuffer(const RHIBufferDesc& desc) noexcept
	{
		GGLAB_ASSERT_MSG(m_Device != nullptr, "DX12ResourceManager must be initialized before creating buffers.");
		GGLAB_ASSERT_MSG(m_Device->GetMemAllocator() != nullptr, "DX12 memory allocator is not initialized.");

		auto buffer = std::make_unique<DX12Buffer>();

		DX12Resource::CreateInfo createInfo{};
		createInfo.m_Allocator = m_Device->GetMemAllocator();
		createInfo.m_AllocDesc.HeapType = D3D12_HEAP_TYPE_DEFAULT;
		createInfo.m_AllocDesc.Flags = D3D12MA::ALLOCATION_FLAG_NONE;
		createInfo.m_ResourceDesc = ToD3D12ResourceDesc(desc);
		createInfo.m_InitStates = D3D12_RESOURCE_STATE_COMMON;
		buffer->Create(createInfo);

		const uint32_t slotIndex = AllocateBufferSlot();
		BufferSlot& slot = m_BufferSlots[slotIndex];
		GGLAB_ASSERT_MSG(!slot.m_Alive, "Allocated RHI buffer slot is already alive.");
		slot.m_Ownership = DX12ResourceOwnership::Owned;
		slot.m_Alive = true;
		slot.m_Buffer = std::move(buffer);

		return RHIBufferHandle(slotIndex, slot.m_Generation);
	}

	void DX12ResourceManager::DestroyTexture(RHITextureHandle texture) noexcept
	{
		if (!IsAlive(texture))
		{
			return;
		}

		TextureSlot& slot = m_TextureSlots[texture.Index()];
		slot.m_Alive = false;
		slot.m_Generation = NextHandleGeneration<RHITextureHandle>(slot.m_Generation);
		m_PendingDestroyedTextures.push_back(std::move(slot.m_Texture));
		m_FreeTextureSlots.push_back(texture.Index());
	}

	void DX12ResourceManager::DestroyBuffer(RHIBufferHandle buffer) noexcept
	{
		if (!IsAlive(buffer))
		{
			return;
		}

		BufferSlot& slot = m_BufferSlots[buffer.Index()];
		slot.m_Alive = false;
		slot.m_Generation = NextHandleGeneration<RHIBufferHandle>(slot.m_Generation);
		m_PendingDestroyedBuffers.push_back(std::move(slot.m_Buffer));
		m_FreeBufferSlots.push_back(buffer.Index());
	}

	bool DX12ResourceManager::IsAlive(RHITextureHandle texture) const noexcept
	{
		if (!texture.IsValid() || texture.Index() >= m_TextureSlots.size())
		{
			return false;
		}

		const TextureSlot& slot = m_TextureSlots[texture.Index()];
		return slot.m_Alive && slot.m_Generation == texture.Generation() && slot.m_Texture != nullptr;
	}

	bool DX12ResourceManager::IsAlive(RHIBufferHandle buffer) const noexcept
	{
		if (!buffer.IsValid() || buffer.Index() >= m_BufferSlots.size())
		{
			return false;
		}

		const BufferSlot& slot = m_BufferSlots[buffer.Index()];
		return slot.m_Alive && slot.m_Generation == buffer.Generation() && slot.m_Buffer != nullptr;
	}

	DX12Texture* DX12ResourceManager::ResolveTexture(RHITextureHandle texture) noexcept
	{
		return const_cast<DX12Texture*>(std::as_const(*this).ResolveTexture(texture));
	}

	const DX12Texture* DX12ResourceManager::ResolveTexture(RHITextureHandle texture) const noexcept
	{
		if (!IsAlive(texture))
		{
			return nullptr;
		}

		return m_TextureSlots[texture.Index()].m_Texture.get();
	}

	DX12Buffer* DX12ResourceManager::ResolveBuffer(RHIBufferHandle buffer) noexcept
	{
		return const_cast<DX12Buffer*>(std::as_const(*this).ResolveBuffer(buffer));
	}

	const DX12Buffer* DX12ResourceManager::ResolveBuffer(RHIBufferHandle buffer) const noexcept
	{
		if (!IsAlive(buffer))
		{
			return nullptr;
		}

		return m_BufferSlots[buffer.Index()].m_Buffer.get();
	}

	void DX12ResourceManager::RetireCompletedResources() noexcept
	{
		m_PendingDestroyedTextures.clear();
		m_PendingDestroyedBuffers.clear();
	}

	uint32_t DX12ResourceManager::AllocateTextureSlot() noexcept
	{
		if (!m_FreeTextureSlots.empty())
		{
			const uint32_t slotIndex = m_FreeTextureSlots.back();
			m_FreeTextureSlots.pop_back();
			return slotIndex;
		}

		const uint32_t slotIndex = static_cast<uint32_t>(m_TextureSlots.size());
		m_TextureSlots.emplace_back();
		return slotIndex;
	}

	uint32_t DX12ResourceManager::AllocateBufferSlot() noexcept
	{
		if (!m_FreeBufferSlots.empty())
		{
			const uint32_t slotIndex = m_FreeBufferSlots.back();
			m_FreeBufferSlots.pop_back();
			return slotIndex;
		}

		const uint32_t slotIndex = static_cast<uint32_t>(m_BufferSlots.size());
		m_BufferSlots.emplace_back();
		return slotIndex;
	}
}
