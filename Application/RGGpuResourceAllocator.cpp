#include "Precompiled.h"
#include "RGGpuResourceAllocator.h"
#include "DX12Device.h"

namespace gglab
{
	RGGpuResourceAllocator::RGGpuResourceAllocator(DX12Device* dx12device) noexcept :
		m_DX12Device(dx12device),
		m_Allocator(dx12device ? dx12device->GetMemAllocator() : nullptr)
	{
		GGLAB_ASSERT_MSG(m_Allocator != nullptr, "Allocator must not be null.");
	}

	void RGGpuResourceAllocator::ReleaseTexture(ResourceIndex texIndex, const DX12FencePoint& fencePoint) noexcept
	{
		if (texIndex < 0 || static_cast<size_t>(texIndex) >= m_Textures.size())
		{
			GGLAB_LOG_WARN("RGGpuResourceAllocator::ReleaseResource() : Release Invalid texture.");
			return;
		}

		//auto* tex = m_Textures[texIndex].get();

		// TODO: last know state
		m_Pendings.emplace_back(
			std::move(Pending
				{
					.m_Type = Type::Texture,
					.m_Index = texIndex,
					.m_FencePoint = fencePoint,
				}));
	}

	void RGGpuResourceAllocator::ReleaseBuffer(ResourceIndex bufIndex, const DX12FencePoint& fencePoint) noexcept
	{
		if (bufIndex < 0 || static_cast<size_t>(bufIndex) >= m_Buffers.size())
		{
			GGLAB_LOG_WARN("RGGpuResourceAllocator::ReleaseResource() : Release Invalid buffer.");
			return;
		}

		//auto* buf = m_Buffers[bufIndex].get();

		// TODO: last know state
		m_Pendings.emplace_back(
			std::move(Pending
				{
					.m_Type = Type::Buffer,
					.m_Index = bufIndex,
					.m_FencePoint = fencePoint,
				}));
	}

	RGGpuResourceAllocator::ResourceIndex RGGpuResourceAllocator::CreateTexture(const RGTextureDesc& rgTexDesc,
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

	RGGpuResourceAllocator::ResourceIndex RGGpuResourceAllocator::CreateBuffer(const RGBufferDesc& rgBufDesc,
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

	DX12Texture* RGGpuResourceAllocator::GetTexture(ResourceIndex texIndex) const noexcept
	{
		if (texIndex == InvalidResourceIndex ||
			static_cast<size_t>(texIndex) >= m_Textures.size())
		{
			GGLAB_LOG_WARN("RGGpuResourceAllocator::GetTexture() : Invalid texIndex.");
			return nullptr;
		}

		return m_Textures[texIndex].get();
	}

	DX12Buffer* RGGpuResourceAllocator::GetBuffer(ResourceIndex bufIndex) const noexcept
	{
		if (bufIndex == InvalidResourceIndex ||
			static_cast<size_t>(bufIndex) >= m_Buffers.size())
		{
			GGLAB_LOG_WARN("RGGpuResourceAllocator::GetBuffer() : Invalid bufIndex.");
			return nullptr;
		}

		return m_Buffers[bufIndex].get();
	}

	void RGGpuResourceAllocator::Tick() noexcept
	{
		std::erase_if(m_Pendings,
			[this](const Pending& pending)
			{
				const auto done = pending.m_FencePoint.IsCompleted();
				if (!done)
				{
					return false;
				}

				switch (pending.m_Type)
				{
				case Type::Texture:
				{
					auto texKey = TextureKey::ConvertResourceDescToKey(m_Textures[pending.m_Index]->GetDesc());
					auto& list = m_FreeTextures[texKey];
					list.push_back(pending.m_Index);

					if (m_MaxCachedPerKey > 0 && list.size() > m_MaxCachedPerKey)
					{
						auto releaseResIndex = list.front();
						list.pop_front();
						m_Textures[releaseResIndex]->Release();
					}
					return true;
				}
				case Type::Buffer:
				{
					auto bufKey = BufferKey::ConvertResourceDescToKey(m_Buffers[pending.m_Index]->GetDesc());
					auto& list = m_FreeBuffers[bufKey];
					list.push_back(pending.m_Index);

					if (m_MaxCachedPerKey > 0 && list.size() > m_MaxCachedPerKey)
					{
						auto releaseResIndex = list.front();
						list.pop_front();
						m_Buffers[releaseResIndex]->Release();
					}
					return true;
				}
				default:
					GGLAB_UNREACHABLE("switch error");
				}
			});
	}

	void RGGpuResourceAllocator::TrimPerKey(uint32_t maxCachedPerKey) noexcept
	{
		m_MaxCachedPerKey = maxCachedPerKey;
		if (maxCachedPerKey == 0) return;

		auto trim = [&](auto& map, auto& storeVec)
			{
				for (auto& keyValue : map)
				{
					auto& list = keyValue.second;
					while (list.size() > maxCachedPerKey)
					{
						auto releaseResIndex = list.front();
						list.pop_front();
						if (releaseResIndex >= 0 && releaseResIndex < static_cast<int32_t>(storeVec.size()) && storeVec[releaseResIndex])
						{
							storeVec[releaseResIndex]->Release();
						}
					}
				}
			};
		trim(m_FreeTextures, m_Textures);
		trim(m_FreeBuffers, m_Buffers);
	}
}