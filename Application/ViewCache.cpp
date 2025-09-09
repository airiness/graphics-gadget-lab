#include "Precompiled.h"
#include "ViewCache.h"
#include "DX12Device.h"
#include "DX12Texture.h"
#include "DX12DescriptorFreeListAllocator.h"

namespace gglab
{
	ViewCache::ViewCache(DX12Device* dx12Device, const DescriptorsAllocatorArray& descriptorAllocators) noexcept :
		m_DX12Device(dx12Device),
		m_DescriptorAllocators(descriptorAllocators)
	{
	}

	ViewCache::~ViewCache()
	{
		GarbageCollect();

		// Release all pending descriptors
		for (auto& pending : m_Pendings)
		{
			for (auto& descriptor : pending.m_Descriptors)
			{
				if (descriptor.IsValid())
				{
					descriptor.Free();
				}
			}
		}
		m_Pendings.clear();

		// Release descriptor still in cache
		for (auto& [key, descriptor] : m_Cache)
		{
			if (descriptor.IsValid())
			{
				descriptor.Free();
			}
		}
		m_Cache.clear();

		m_ResourceViews.clear();
	}

	const DX12Descriptor& ViewCache::GetRTV(ResourceIndex resourceIndex, DX12Texture* texture,
		std::optional<D3D12_RENDER_TARGET_VIEW_DESC> descOpt) noexcept
	{
		// TODO: insert return statement here
	}

	const DX12Descriptor& ViewCache::GetDSV(ResourceIndex resourceIndex, DX12Texture* texture,
		std::optional<D3D12_DEPTH_STENCIL_VIEW_DESC> descOpt) noexcept
	{
		// TODO: insert return statement here
	}

	const DX12Descriptor& ViewCache::GetSRV(ResourceIndex resourceIndex, DX12Texture* texture,
		std::optional<D3D12_SHADER_RESOURCE_VIEW_DESC> descOpt) noexcept
	{
		// TODO: insert return statement here
	}

	const DX12Descriptor& ViewCache::GetUAV(ResourceIndex resourceIndex, DX12Texture* texture,
		std::optional<D3D12_UNORDERED_ACCESS_VIEW_DESC> descOpt) noexcept
	{
		// TODO: insert return statement here
	}

	void ViewCache::RetireResourceAllViews(ResourceIndex resourceIndex, const DX12FencePoint& fencePoint) noexcept
	{
		std::scoped_lock lock(m_Mutex);

		auto it = m_ResourceViews.find(resourceIndex);
		if (it == m_ResourceViews.end())
		{
			return;
		}

		Pending pending
		{
			.m_ResourceIndex = resourceIndex,
			.m_FencePoint = fencePoint
		};

		pending.m_Descriptors.reserve(it->second.size());
		for (const auto& key : it->second)
		{
			auto descriptorIt = m_Cache.find(key);
			if (descriptorIt == m_Cache.end())
			{
				continue;
			}

			pending.m_Descriptors.push_back(std::move(descriptorIt->second));
			m_Cache.erase(descriptorIt);
		}

		m_ResourceViews.erase(it);

		if (!pending.m_Descriptors.empty())
		{
			m_Pendings.emplace_back(std::move(pending));
		}
	}

	void ViewCache::GarbageCollect() noexcept
	{
		std::scoped_lock lock(m_Mutex);
		while (!m_Pendings.empty())
		{
			auto& pending = m_Pendings.front();
			if (!pending.m_FencePoint.IsCompleted())
			{
				break;
			}

			for (auto& descriptor : pending.m_Descriptors)
			{
				if (descriptor.IsValid())
				{
					descriptor.Free();
				}
			}

			m_Pendings.pop_front();
		}
	}

	void ViewCache::FreeAllImmediately(ResourceIndex resourceIndex) noexcept
	{
		std::scoped_lock lock(m_Mutex);

		if (auto it = m_ResourceViews.find(resourceIndex); it != m_ResourceViews.end())
		{
			for (const auto& key : it->second)
			{
				if (auto descriptorIt = m_Cache.find(key); descriptorIt != m_Cache.end())
				{
					if (descriptorIt->second.IsValid())
					{
						descriptorIt->second.Free();
					}
					m_Cache.erase(descriptorIt);
				}
			}
			m_ResourceViews.erase(it);
		}

		if (!m_Pendings.empty())
		{
			auto pendingIt = m_Pendings.begin();
			while (pendingIt != m_Pendings.end())
			{
				if (pendingIt->m_ResourceIndex == resourceIndex)
				{
					for (auto& descriptor : pendingIt->m_Descriptors)
					{
						if (descriptor.IsValid())
						{
							descriptor.Free();
						}
					}
					pendingIt = m_Pendings.erase(pendingIt);
				}
				else
				{
					++pendingIt;
				}
			}
		}
	}

	DX12DescriptorFreeListAllocator* ViewCache::GetDescriptorAllocator(Type type) const noexcept
	{
		return m_DescriptorAllocators[static_cast<uint32_t>(type)];
	}

	template<ViewCache::Type T>
	const DX12Descriptor& ViewCache::GetOrCreateImpl(ResourceIndex resourceIndex,
		DX12Texture* texture,
		std::optional<typename ViewTraits<T>::Desc> descOpt) noexcept
	{
		return DX12Descriptor();
	}
}