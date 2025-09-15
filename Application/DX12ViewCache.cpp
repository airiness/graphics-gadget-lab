#include "Precompiled.h"
#include "DX12ViewCache.h"
#include "DX12Device.h"
#include "DX12Texture.h"
#include "DX12DescriptorFreeListAllocator.h"

namespace gglab
{
	DX12ViewCache::DX12ViewCache(DX12Device* dx12Device, const DescriptorsAllocatorArray& descriptorAllocators) noexcept :
		m_DX12Device(dx12Device),
		m_DescriptorAllocators(descriptorAllocators)
	{
	}

	DX12ViewCache::~DX12ViewCache()
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

	const DX12Descriptor& DX12ViewCache::GetRTV(ResourceIndex resourceIndex, DX12Texture* texture,
		std::optional<D3D12_RENDER_TARGET_VIEW_DESC> descOpt) noexcept
	{
		return GetOrCreateImpl<ViewType::RTV>(resourceIndex, texture, descOpt);
	}

	const DX12Descriptor& DX12ViewCache::GetDSV(ResourceIndex resourceIndex, DX12Texture* texture,
		std::optional<D3D12_DEPTH_STENCIL_VIEW_DESC> descOpt) noexcept
	{
		return GetOrCreateImpl<ViewType::DSV>(resourceIndex, texture, descOpt);
	}

	const DX12Descriptor& DX12ViewCache::GetSRV(ResourceIndex resourceIndex, DX12Texture* texture,
		std::optional<D3D12_SHADER_RESOURCE_VIEW_DESC> descOpt) noexcept
	{
		return GetOrCreateImpl<ViewType::SRV>(resourceIndex, texture, descOpt);
	}

	const DX12Descriptor& DX12ViewCache::GetUAV(ResourceIndex resourceIndex, DX12Texture* texture,
		std::optional<D3D12_UNORDERED_ACCESS_VIEW_DESC> descOpt) noexcept
	{
		return GetOrCreateImpl<ViewType::UAV>(resourceIndex, texture, descOpt);
	}

	void DX12ViewCache::RetireResourceAllViews(ResourceIndex resourceIndex, const DX12FencePoint& fencePoint) noexcept
	{
		std::scoped_lock lock(m_Mutex);

		auto it = m_ResourceViews.find(resourceIndex);
		if (it == m_ResourceViews.end())
		{
			return;
		}

		Pending pending{};
		pending.m_ResourceIndex = resourceIndex;
		pending.m_FencePoint = fencePoint;
		pending.m_Descriptors.reserve(it->second.size());

		for (const auto& key : it->second)
		{
			auto descriptorIt = m_Cache.find(key);
			if (descriptorIt != m_Cache.end())
			{
				pending.m_Descriptors.push_back(std::move(descriptorIt->second));
				m_Cache.erase(descriptorIt);
			}
		}

		m_ResourceViews.erase(it);

		if (!pending.m_Descriptors.empty())
		{
			m_Pendings.emplace_back(std::move(pending));
		}
	}

	void DX12ViewCache::GarbageCollect() noexcept
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

	void DX12ViewCache::FreeAllImmediately(ResourceIndex resourceIndex) noexcept
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

	DX12DescriptorFreeListAllocator* DX12ViewCache::GetDescriptorAllocator(ViewType type) const noexcept
	{
		switch (type)
		{
		case ViewType::RTV:
		case ViewType::DSV:
			return m_DescriptorAllocators[static_cast<uint32_t>(type)];
		case ViewType::SRV:
		case ViewType::UAV:
			return m_DescriptorAllocators[static_cast<uint32_t>(ViewType::SRV)];
		default:
			GGLAB_UNREACHABLE("Unknow View Type.");
			break;
		}
	}

	template<ViewType T>
	const DX12Descriptor& DX12ViewCache::GetOrCreateImpl(ResourceIndex resourceIndex,
		DX12Texture* texture,
		std::optional<typename ViewTraits<T>::Desc> descOpt) noexcept
	{
		using Traits = ViewTraits<T>;
		const typename Traits::Desc inDesc = descOpt.value_or(typename Traits::Desc{});

		const auto built = Traits::Build(resourceIndex, texture, inDesc);
		const auto& key = built.m_Key;

		// Check the Key has existed in cache
		{
			std::scoped_lock lock(m_Mutex);
			if (auto it = m_Cache.find(key); it != m_Cache.end())
			{
				return it->second;
			}
		}

		// Allocate and create
		DX12Descriptor descriptor = GetDescriptorAllocator(T)->Allocate(1);
		Traits::Create(m_DX12Device, texture, &built.m_Desc, descriptor);
		{
			std::scoped_lock lock(m_Mutex);
			if (auto it = m_Cache.find(key); it != m_Cache.end())
			{
				// Other thread inserted, release it
				if (descriptor.IsValid())
				{
					descriptor.Free();
				}
			}

			m_ResourceViews[resourceIndex].push_back(key);
			auto [it, inserted] = m_Cache.emplace(key, std::move(descriptor));
			if (!inserted)
			{
				it->second.Free();
			}
			return it->second;
		}
	}

	auto ViewTraits<ViewType::RTV>::Build(ResourceIndex resourceIndex,
		DX12Texture* texture, const Desc& inDesc) noexcept -> Built
	{
		Built built{};
		auto& outDesc = built.m_Desc;
		const auto& texDesc = texture->GetDesc();
		outDesc = inDesc;

		if (outDesc.Format == DXGI_FORMAT_UNKNOWN)
		{
			outDesc.Format = texDesc.Format;// TODO: typeless->typed?
		}

		if (outDesc.ViewDimension == D3D12_RTV_DIMENSION_UNKNOWN)
		{
			// Sample count is bigger than 1, multi-sample
			outDesc.ViewDimension = (texDesc.SampleDesc.Count > 1) ?
				D3D12_RTV_DIMENSION_TEXTURE2DMS :
				D3D12_RTV_DIMENSION_TEXTURE2D;
		}

		uint16_t mip = 0;
		uint8_t plane = 0;
		if (outDesc.ViewDimension == D3D12_RTV_DIMENSION_TEXTURE2D)
		{
			mip = static_cast<uint16_t>(outDesc.Texture2D.MipSlice);
			plane = static_cast<uint8_t>(outDesc.Texture2D.PlaneSlice);
		}

		auto& key = built.m_Key;
		key.m_Type = ViewType::RTV;
		key.m_ResouceIndexValue = resourceIndex.Value();
		key.m_Format = outDesc.Format;
		key.m_MipSlice = mip;
		key.m_ArraySlice = 0;
		key.m_PlaneSlice = plane;
		key.m_Dimension = static_cast<uint8_t>(outDesc.ViewDimension);
		key.m_Flags = 0;
		key.m_ComponentMapping = 0;
		key.m_MipLevels = 0;

		return built;
	}

	void ViewTraits<ViewType::RTV>::Create(DX12Device* device,
		DX12Texture* texture, const Desc* desc, const DX12Descriptor& outDesc) noexcept
	{
		device->Get()->CreateRenderTargetView(texture->Get(), desc, outDesc.CpuHandle());
	}

	auto ViewTraits<ViewType::DSV>::Build(ResourceIndex resourceIndex,
		DX12Texture* texture, const Desc& inDesc) noexcept -> Built
	{
		Built built{};
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

		uint16_t mip = 0;
		if (outDesc.ViewDimension == D3D12_DSV_DIMENSION_TEXTURE2D)
		{
			mip = static_cast<uint16_t>(outDesc.Texture2D.MipSlice);
		}

		// Encode flags
		const auto encodeDsvFlags = [](D3D12_DSV_FLAGS flags) noexcept
			{
				using U = std::underlying_type_t<D3D12_DSV_FLAGS>;
				constexpr uint8_t kBits = static_cast<uint8_t>(
					static_cast<U>(D3D12_DSV_FLAG_READ_ONLY_DEPTH) |
					static_cast<U>(D3D12_DSV_FLAG_READ_ONLY_STENCIL));

				return static_cast<uint8_t>(static_cast<U>(flags)) & kBits;
			};

		auto& key = built.m_Key;
		key.m_Type = ViewType::DSV;
		key.m_ResouceIndexValue = resourceIndex.Value();
		key.m_Format = outDesc.Format;
		key.m_MipSlice = mip;
		key.m_ArraySlice = 0;
		key.m_PlaneSlice = 0;
		key.m_Dimension = static_cast<uint8_t>(outDesc.ViewDimension);
		key.m_Flags = encodeDsvFlags(outDesc.Flags);
		key.m_ComponentMapping = 0;
		key.m_MipLevels = 0;

		return built;
	}

	void ViewTraits<ViewType::DSV>::Create(DX12Device* device,
		DX12Texture* texture, const Desc* desc, const DX12Descriptor& descriptor) noexcept
	{
		device->Get()->CreateDepthStencilView(texture->Get(), desc, descriptor.CpuHandle());
	}

	auto ViewTraits<ViewType::SRV>::Build(ResourceIndex resourceIndex,
		DX12Texture* texture, const Desc& inDesc) noexcept -> Built
	{
		Built built{};
		auto& outDesc = built.m_Desc;
		const auto& texDesc = texture->GetDesc();
		outDesc = inDesc;

		if (outDesc.Format == DXGI_FORMAT_UNKNOWN)
		{
			outDesc.Format = texDesc.Format; // TODO: sRGB/ linear
		}

		if (outDesc.Shader4ComponentMapping == 0)
		{
			outDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		}

		if (outDesc.ViewDimension == D3D12_SRV_DIMENSION_UNKNOWN)
		{
			outDesc.ViewDimension = (texDesc.SampleDesc.Count > 1) ?
				D3D12_SRV_DIMENSION_TEXTURE2DMS :
				D3D12_SRV_DIMENSION_TEXTURE2D;
		}

		uint16_t most = 0;
		uint16_t levels = 0;
		uint8_t plane = 0;
		if (outDesc.ViewDimension == D3D12_SRV_DIMENSION_TEXTURE2D)
		{
			most = static_cast<uint16_t>(outDesc.Texture2D.MostDetailedMip);
			plane = static_cast<uint8_t>(outDesc.Texture2D.PlaneSlice);
			levels = (outDesc.Texture2D.MipLevels == UINT(-1)) ?
				0 :
				static_cast<uint16_t>(outDesc.Texture2D.MipLevels);
		}

		auto& key = built.m_Key;
		key.m_Type = ViewType::SRV;
		key.m_ResouceIndexValue = resourceIndex.Value();
		key.m_Format = outDesc.Format;
		key.m_MipSlice = most;
		key.m_MipLevels = levels;
		key.m_ArraySlice = 0;
		key.m_PlaneSlice = plane;
		key.m_Dimension = static_cast<uint8_t>(outDesc.ViewDimension);
		key.m_Flags = 0;
		key.m_ComponentMapping = outDesc.Shader4ComponentMapping;

		return built;
	}

	void ViewTraits<ViewType::SRV>::Create(DX12Device* device,
		DX12Texture* texture, const Desc* desc, const DX12Descriptor& descriptor) noexcept
	{
		device->Get()->CreateShaderResourceView(texture->Get(), desc, descriptor.CpuHandle());
	}

	auto ViewTraits<ViewType::UAV>::Build(ResourceIndex resourceIndex,
		DX12Texture* texture, const Desc& inDesc) noexcept -> Built
	{
		Built built{};
		auto& outDesc = built.m_Desc;
		const auto& texDesc = texture->GetDesc();
		outDesc = inDesc;

		if (outDesc.Format == DXGI_FORMAT_UNKNOWN)
		{
			outDesc.Format = texDesc.Format;
		}

		if (outDesc.ViewDimension == D3D12_UAV_DIMENSION_UNKNOWN)
		{
			outDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
		}

		uint16_t mip = 0;
		uint8_t plane = 0;
		if (outDesc.ViewDimension == D3D12_UAV_DIMENSION_TEXTURE2D)
		{
			mip = static_cast<uint16_t>(outDesc.Texture2D.MipSlice);
			plane = static_cast<uint8_t>(outDesc.Texture2D.PlaneSlice);
		}

		auto& key = built.m_Key;
		key.m_Type = ViewType::UAV;
		key.m_ResouceIndexValue = resourceIndex.Value();
		key.m_Format = outDesc.Format;
		key.m_MipSlice = mip;
		key.m_ArraySlice = 0;
		key.m_PlaneSlice = plane;
		key.m_Dimension = static_cast<uint8_t>(outDesc.ViewDimension);
		key.m_Flags = 0;
		key.m_ComponentMapping = 0;
		key.m_MipLevels = 0;

		return built;
	}

	void ViewTraits<ViewType::UAV>::Create(DX12Device* device,
		DX12Texture* texture, const Desc* desc, const DX12Descriptor& descriptor) noexcept
	{
		device->Get()->CreateUnorderedAccessView(texture->Get(), nullptr, desc, descriptor.CpuHandle());
	}

#define DECL_GET_OR_CREATE_TEMPLATE_FUNC(viewType, descType)	\
	template const DX12Descriptor& DX12ViewCache::GetOrCreateImpl<viewType>(ResourceIndex, DX12Texture*, std::optional<descType>) noexcept;

	DECL_GET_OR_CREATE_TEMPLATE_FUNC(ViewType::RTV, D3D12_RENDER_TARGET_VIEW_DESC);
	DECL_GET_OR_CREATE_TEMPLATE_FUNC(ViewType::DSV, D3D12_DEPTH_STENCIL_VIEW_DESC);
	DECL_GET_OR_CREATE_TEMPLATE_FUNC(ViewType::SRV, D3D12_SHADER_RESOURCE_VIEW_DESC);
	DECL_GET_OR_CREATE_TEMPLATE_FUNC(ViewType::UAV, D3D12_UNORDERED_ACCESS_VIEW_DESC);

#undef DECL_GET_OR_CREATE_TEMPLATE_FUNC
}