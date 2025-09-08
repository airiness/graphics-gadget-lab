#include "Precompiled.h"
#include "ViewCache.h"
#include "DX12Device.h"
#include "DX12Texture.h"
#include "DX12DescriptorFreeListAllocator.h"

namespace gglab
{
	template<> struct ViewTraits<ViewCache::Type::RTV>
	{
		using Desc = D3D12_RENDER_TARGET_VIEW_DESC;

		struct Built
		{
			ViewCache::ViewKey m_Key;
			Desc m_Desc;
		};

		static Built Build(ViewCache::ResourceIndex resourceIndex, DX12Texture* texture, const Desc& inDesc) noexcept;
	};



	ViewCache::BuiltRTV ViewCache::CreateRTVKey(uint32_t resourceIndex,
		DX12Texture* texture,
		const D3D12_RENDER_TARGET_VIEW_DESC& inDesc) noexcept
	{
		BuiltRTV built{};
		auto& outDesc = built.m_Desc;

		const auto& texDesc = texture->GetDesc();
		outDesc = inDesc;

		// TODO: typeless resource? format mapping?
		if (outDesc.Format == DXGI_FORMAT_UNKNOWN)
		{
			outDesc.Format = texDesc.Format;
		}

		if (outDesc.ViewDimension == D3D12_RTV_DIMENSION_UNKNOWN)
		{
			outDesc.ViewDimension = (texDesc.SampleDesc.Count > 1) ?
				D3D12_RTV_DIMENSION_TEXTURE2DMS :
				D3D12_RTV_DIMENSION_TEXTURE2D;
		}

		uint16_t mipSlice = 0;
		uint8_t planeSlice = 0;

		switch (outDesc.ViewDimension)
		{
		case D3D12_RTV_DIMENSION_TEXTURE2D:
			mipSlice = static_cast<uint16_t>(outDesc.Texture2D.MipSlice);
			planeSlice = static_cast<uint8_t>(outDesc.Texture2D.PlaneSlice);
			break;
		case D3D12_RTV_DIMENSION_TEXTURE2DMS:
			break;
		default:
			break;
		}

		auto& key = built.m_ViewKey;
		key.m_Type = Type::RTV;
		key.m_ResouceIndex = resourceIndex;
		key.m_Format = outDesc.Format;
		key.m_MipSlice = mipSlice;
		key.m_ArraySlice = 0;
		key.m_PlaneSlice = planeSlice;
		key.m_Dimension = static_cast<uint8_t>(outDesc.ViewDimension);
		key.m_Flags = 0;

		return built;
	}

	ViewCache::BuiltDSV ViewCache::CreateDSVKey(uint32_t resourceIndex,
		DX12Texture* texture,
		const D3D12_DEPTH_STENCIL_VIEW_DESC& inDesc) noexcept
	{
		BuiltDSV built{};
		auto& outDesc = built.m_Desc;

		const auto& texDesc = texture->GetDesc();
		outDesc = inDesc;

		if (outDesc.Format == DXGI_FORMAT_UNKNOWN)
		{
			outDesc.Format = texDesc.Format;
		}

		if (outDesc.ViewDimension == D3D12_DSV_DIMENSION_UNKNOWN)
		{
			outDesc.ViewDimension = (texDesc.SampleDesc.Count > 1) ?
				D3D12_DSV_DIMENSION_TEXTURE2DMS :
				D3D12_DSV_DIMENSION_TEXTURE2D;
		}

		uint16_t mipSlice = 0;
		switch (outDesc.ViewDimension)
		{
		case D3D12_DSV_DIMENSION_TEXTURE2D:
			mipSlice = static_cast<uint16_t>(outDesc.Texture2D.MipSlice);
			break;
		case D3D12_DSV_DIMENSION_TEXTURE2DMS:
			break;
		default:
			break;
		}

		constexpr auto encodeDsvFlags = [](D3D12_DSV_FLAGS flags)
			{
				using U = std::underlying_type_t<D3D12_DSV_FLAGS>;
				constexpr uint8_t bits = static_cast<uint8_t>(
					static_cast<U>(D3D12_DSV_FLAG_READ_ONLY_DEPTH) |
					static_cast<U>(D3D12_DSV_FLAG_READ_ONLY_STENCIL));

				return static_cast<uint8_t>(static_cast<U>(flags)) & bits;
			};

		auto& key = built.m_ViewKey;
		key.m_Type = Type::DSV;
		key.m_ResouceIndex = resourceIndex;
		key.m_Format = outDesc.Format;
		key.m_MipSlice = mipSlice;
		key.m_ArraySlice = 0;
		key.m_PlaneSlice = 0;
		key.m_Dimension = static_cast<uint8_t>(outDesc.ViewDimension);
		key.m_Flags = encodeDsvFlags(outDesc.Flags);

		return built;
	}

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

	const DX12Descriptor& ViewCache::GetRTV(uint32_t resourceIndex,
		DX12Texture* texture,
		std::optional<D3D12_RENDER_TARGET_VIEW_DESC> desc) noexcept
	{
		// TODO: Need lock here?
		std::scoped_lock lock(m_Mutex);

		const auto inDesc = desc.value_or(D3D12_RENDER_TARGET_VIEW_DESC{});
		auto built = CreateRTVKey(resourceIndex, texture, inDesc);

		const auto& key = built.m_ViewKey;
		const auto& rtvDesc = built.m_Desc;

		if (auto it = m_Cache.find(key); it != m_Cache.end())
		{
			return it->second;
		}

		auto* allocator = GetDescriptorAllocator(Type::RTV);
		DX12Descriptor descriptor = allocator->Allocate(1);
		m_DX12Device->Get()->CreateRenderTargetView(texture->Get(), &rtvDesc, descriptor.CpuHandle());

		m_ResourceViews[resourceIndex].push_back(key);
		auto [it, inserted] = m_Cache.emplace(key, std::move(descriptor));
		return it->second;
	}

	const DX12Descriptor& ViewCache::GetDepthStencilView(uint32_t resourceIndex, 
		DX12Texture* texture, 
		std::optional<D3D12_DEPTH_STENCIL_VIEW_DESC> desc) noexcept
	{
		std::scoped_lock lock(m_Mutex);

		const auto inDesc = desc.value_or(D3D12_DEPTH_STENCIL_VIEW_DESC{});
		auto built = CreateDSVKey(resourceIndex, texture, inDesc);

		const auto& key = built.m_ViewKey;
		const auto& dsvDesc = built.m_Desc;

		if (auto it = m_Cache.find(key); it != m_Cache.end())
		{
			return it->second;
		}

		auto* allocator = GetDescriptorAllocator(Type::DSV);
		DX12Descriptor descriptor = allocator->Allocate(1);
		m_DX12Device->Get()->CreateDepthStencilView(texture->Get(), &dsvDesc, descriptor.CpuHandle());

		m_ResourceViews[resourceIndex].push_back(key);
		auto [it, inserted] = m_Cache.emplace(key, std::move(descriptor));
		return it->second;
	}

	void ViewCache::RetireResourceAllViews(uint32_t resourceIndex, const DX12FencePoint& fencePoint) noexcept
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

	void ViewCache::RetireResourceAllViewsImmediately(uint32_t resourceIndex) noexcept
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
}