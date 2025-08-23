#include "Precompiled.h"
#include "RGGpuResAllocator.h"
#include "DX12Device.h"

namespace gglab
{
	RGGpuResAllocator::RGGpuResAllocator(DX12Device* dx12device) noexcept :
		m_DX12Device(dx12device),
		m_Allocator(dx12device ? dx12device->GetMemAllocator() : nullptr)
	{
		GGLAB_ASSERT_MSG(m_Allocator != nullptr, "Allocator must not be null.");
	}

	RGGpuResAllocator::ResourceIndex RGGpuResAllocator::CreateTexture(const RGTextureDesc& rgTexDesc, 
		D3D12_RESOURCE_STATES initStates, 
		std::optional<D3D12_CLEAR_VALUE> clearValue) noexcept
	{
		ResourceIndex index = static_cast<ResourceIndex>(m_Textures.size());
		m_Textures.emplace_back(std::make_unique<DX12Texture>());
		auto& tex = m_Textures.back();

		DX12Resource::CreateInfo createInfo = {};
		createInfo.m_Allocator = m_Allocator;
		createInfo.m_AllocDesc.HeapType = D3D12_HEAP_TYPE_DEFAULT;
		createInfo.m_AllocDesc.Flags =
			Test(rgTexDesc.m_Usage, RGTextureUsage::RenderTarget | RGTextureUsage::DepthStencil) ?
			D3D12MA::ALLOCATION_FLAG_COMMITTED : D3D12MA::ALLOCATION_FLAG_NONE; // RenderTarget & DepthStencil is big resource use Allocation type committed.
		createInfo.m_InitStates = initStates;
		createInfo.m_ClearValue = clearValue;
		createInfo.m_ResourceDesc = ToD3D12ResourceDesc(rgTexDesc);
		tex->Create(createInfo);

		return index;
	}

	RGGpuResAllocator::ResourceIndex RGGpuResAllocator::CreateBuffer(const RGBufferDesc& rgBufDesc,
		D3D12_RESOURCE_STATES initStates) noexcept
	{
		ResourceIndex index = static_cast<ResourceIndex>(m_Buffers.size());
		m_Buffers.emplace_back(std::make_unique<DX12Buffer>());
		auto& buf = m_Buffers.back();

		DX12Resource::CreateInfo createInfo = {};
		createInfo.m_Allocator = m_Allocator;
		createInfo.m_AllocDesc.HeapType = D3D12_HEAP_TYPE_DEFAULT;
		createInfo.m_AllocDesc.Flags = D3D12MA::ALLOCATION_FLAG_NONE;
		createInfo.m_InitStates = initStates;
		createInfo.m_ResourceDesc = ToD3D12ResourceDesc(rgBufDesc);
		buf->Create(createInfo);

		return index;
	}

	DX12Texture* RGGpuResAllocator::GetTexture(ResourceIndex texIndex) const noexcept
	{
		if (texIndex == InvalidResourceIndex ||
			static_cast<size_t>(texIndex) >= m_Textures.size())
		{
			GGLAB_LOG_WARN("RGGpuResAllocator::GetTexture() : Invalid texIndex.");
			return nullptr;
		}

		return m_Textures[texIndex].get();
	}

	DX12Buffer* RGGpuResAllocator::GetBuffer(ResourceIndex bufIndex) const noexcept
	{
		if (bufIndex == InvalidResourceIndex ||
			static_cast<size_t>(bufIndex) >= m_Buffers.size())
		{
			GGLAB_LOG_WARN("RGGpuResAllocator::GetBuffer() : Invalid bufIndex.");
			return nullptr;
		}

		return m_Buffers[bufIndex].get();
	}

	void RGGpuResAllocator::Tick(DX12FencePoint fencePoint) noexcept
	{
		for (size_t i = 0; i < m_Pendings.size();)
		{
			auto done = false;
			auto& pending = m_Pendings[i];

			done = pending.m_FencePoint.IsCompleted();

			if (!done)
			{
				++i;
				continue;
			}
		}


	}

	//void RGGpuResAllocator::ReleaseTexture(TextureHandle texHandle) noexcept
	//{
	//	if (texHandle == InvalidTextureHandle)
	//	{
	//		return;
	//	}

	//	GGLAB_ASSERT_MSG(static_cast<size_t>(texHandle) < m_TextureSlots.size(), "Invalid Texture Handle.");

	//	auto& texSlot = m_TextureSlots[texHandle];
	//	if (texSlot.m_Texture)
	//	{
	//		texSlot.m_Texture->Release();
	//	}

	//	texSlot.m_Texture.reset();
	//	texSlot.m_IsInUse = false;
	//}

	//void RGGpuResAllocator::ReleaseBuffer(BufferHandle bufHandle) noexcept
	//{
	//	if (bufHandle == InvalidBufferHandle)
	//	{
	//		return;
	//	}

	//	GGLAB_ASSERT_MSG(static_cast<size_t>(bufHandle) < m_BufferSlots.size(), "Invalid Buffer Handle.");

	//	auto& bufSlot = m_BufferSlots[bufHandle];
	//	if (bufSlot.m_Buffer)
	//	{
	//		bufSlot.m_Buffer->Release();
	//	}

	//	bufSlot.m_Buffer.reset();
	//	bufSlot.m_IsInUse = false;

	//}



	//RGGpuResAllocator::TextureHandle RGGpuResAllocator::AllocateTextureSlot() noexcept
	//{
	//	const auto slotsSize = static_cast<TextureHandle>(m_TextureSlots.size());

	//	for (TextureHandle h = 0; h < slotsSize; ++h)
	//	{
	//		const auto& texSlot = m_TextureSlots[h];
	//		if (!texSlot.m_IsInUse && texSlot.m_Texture == nullptr)
	//		{
	//			return h;
	//		}
	//	}

	//	m_TextureSlots.push_back({});
	//	return slotsSize;
	//}

	//RGGpuResAllocator::BufferHandle RGGpuResAllocator::AllocateBufferSlot() noexcept
	//{
	//	const auto slotsSize = static_cast<BufferHandle>(m_BufferSlots.size());

	//	for (TextureHandle h = 0; h < slotsSize; ++h)
	//	{
	//		const auto& bufSlot = m_BufferSlots[h];
	//		if (!bufSlot.m_IsInUse && bufSlot.m_Buffer == nullptr)
	//		{
	//			return h;
	//		}
	//	}

	//	m_BufferSlots.push_back({});
	//	return slotsSize;
	//}
}