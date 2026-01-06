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
		for (auto& pending : m_Pending)
		{
			for (auto& descriptor : pending.m_Descriptors)
			{
				if (descriptor.IsValid())
				{
					descriptor.Free();
				}
			}
		}
		m_Pending.clear();

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

	const DX12DescriptorHandle& DX12ViewCache::GetOrCreate(const ViewKey& key, DX12Texture* texture) noexcept
	{
		{
			std::shared_lock lock(m_Mutex);
			if (auto iter = m_Cache.find(key); iter != m_Cache.end())
			{
				return iter->second;
			}
		}

		switch (key.m_Type)
		{
		case ViewType::RTV: return CreateFromKey<ViewType::RTV>(key, texture);
		case ViewType::DSV: return CreateFromKey<ViewType::DSV>(key, texture);
		case ViewType::SRV: return CreateFromKey<ViewType::SRV>(key, texture);
		case ViewType::UAV: return CreateFromKey<ViewType::UAV>(key, texture);
		default: GGLAB_UNREACHABLE("Unkown View Tyoe.");
		}
	}

	void DX12ViewCache::RetireResourceAllViews(ResourceIndex resourceIndex, const DX12FencePoint& fencePoint) noexcept
	{
		std::unique_lock lock(m_Mutex);

		auto iter = m_ResourceViews.find(resourceIndex);
		if (iter == m_ResourceViews.end())
		{
			return;
		}

		Pending pending{};
		pending.m_ResourceIndex = resourceIndex;
		pending.m_FencePoint = fencePoint;
		pending.m_Descriptors.reserve(iter->second.size());

		for (const auto& key : iter->second)
		{
			auto descriptorIt = m_Cache.find(key);
			if (descriptorIt != m_Cache.end())
			{
				pending.m_Descriptors.push_back(std::move(descriptorIt->second));
				m_Cache.erase(descriptorIt);
			}
		}

		m_ResourceViews.erase(iter);

		if (!pending.m_Descriptors.empty())
		{
			m_Pending.emplace_back(std::move(pending));
		}
	}

	void DX12ViewCache::GarbageCollect() noexcept
	{
		std::unique_lock lock(m_Mutex);
		while (!m_Pending.empty())
		{
			auto& pending = m_Pending.front();
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

			m_Pending.pop_front();
		}
	}

	void DX12ViewCache::FreeAllImmediately(ResourceIndex resourceIndex) noexcept
	{
		std::unique_lock lock(m_Mutex);

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

		if (!m_Pending.empty())
		{
			auto pendingIt = m_Pending.begin();
			while (pendingIt != m_Pending.end())
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
					pendingIt = m_Pending.erase(pendingIt);
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
			GGLAB_UNREACHABLE("Unknown View Type.");
		}
	}

	template<ViewType T>
	const DX12DescriptorHandle& DX12ViewCache::GetOrCreateImpl(ResourceIndex resourceIndex, DX12Texture* texture,
		std::optional<typename ViewTraits<T>::Desc> descOpt) noexcept
	{
		using Traits = ViewTraits<T>;
		const typename Traits::Desc inDesc = descOpt.value_or(typename Traits::Desc{});

		const auto built = Traits::Build(resourceIndex, texture, inDesc);
		const auto& key = built.m_Key;

		// Check the Key has existed in cache
		{
			std::shared_lock lock(m_Mutex);
			if (auto iter = m_Cache.find(key); iter != m_Cache.end())
			{
				return iter->second;
			}
		}

		// Allocate and create
		DX12DescriptorHandle descriptor = GetDescriptorAllocator(T)->Allocate(1);
		Traits::Create(m_DX12Device, texture, &built.m_Desc, descriptor);
		{
			std::unique_lock lock(m_Mutex);
			if (auto it = m_Cache.find(key); it != m_Cache.end())
			{
				// Other thread inserted, release it
				if (descriptor.IsValid())
				{
					descriptor.Free();
				}
				return it->second;
			}
	
			auto [it, inserted] = m_Cache.emplace(key, std::move(descriptor));
			GGLAB_ASSERT_MSG(inserted, "ViewCache: duplicate insertion after double-checked locking");
			m_ResourceViews[resourceIndex].push_back(key);
			return it->second;
		}
	}

	template<ViewType T>
	const DX12DescriptorHandle& DX12ViewCache::CreateFromKey(const ViewKey& key, DX12Texture* texture) noexcept
	{
		using Traits = ViewTraits<T>;
		auto desc = DescFromKey<T>(key);

		DX12DescriptorHandle descriptor = GetDescriptorAllocator(key.m_Type)->Allocate(1);
		Traits::Create(m_DX12Device, texture, &desc, descriptor);
		{
			std::unique_lock lock(m_Mutex);
			if (auto it = m_Cache.find(key); it != m_Cache.end())
			{
				if (descriptor.IsValid())
				{
					descriptor.Free();
				}
				return it->second;
			}

			auto [it, inserted] = m_Cache.emplace(key, std::move(descriptor));
			GGLAB_ASSERT_MSG(inserted, "ViewCache: duplicate insertion after double-checked locking");
			m_ResourceViews[key.m_ResouceIndex].push_back(key);
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
		key.m_ResouceIndex = resourceIndex;
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
		DX12Texture* texture, const Desc* desc, const DX12DescriptorHandle& outDesc) noexcept
	{
		device->Get()->CreateRenderTargetView(texture->Get(), desc, outDesc.CpuHandleAt());
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
		key.m_ResouceIndex = resourceIndex;
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
		DX12Texture* texture, const Desc* desc, const DX12DescriptorHandle& descriptor) noexcept
	{
		device->Get()->CreateDepthStencilView(texture->Get(), desc, descriptor.CpuHandleAt());
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
		key.m_ResouceIndex = resourceIndex;
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
		DX12Texture* texture, const Desc* desc, const DX12DescriptorHandle& descriptor) noexcept
	{
		device->Get()->CreateShaderResourceView(texture->Get(), desc, descriptor.CpuHandleAt());
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
		key.m_ResouceIndex = resourceIndex;
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
		DX12Texture* texture, const Desc* desc, const DX12DescriptorHandle& descriptor) noexcept
	{
		device->Get()->CreateUnorderedAccessView(texture->Get(), nullptr, desc, descriptor.CpuHandleAt());
	}


	template<>
	inline D3D12_RENDER_TARGET_VIEW_DESC DX12ViewCache::DescFromKey<ViewType::RTV>(const ViewKey& key) noexcept
	{
		D3D12_RENDER_TARGET_VIEW_DESC d{};
		d.Format = key.m_Format;
		d.ViewDimension = static_cast<D3D12_RTV_DIMENSION>(key.m_Dimension);
		if (d.ViewDimension == D3D12_RTV_DIMENSION_TEXTURE2D)
		{
			d.Texture2D.MipSlice = key.m_MipSlice;
			d.Texture2D.PlaneSlice = key.m_PlaneSlice;
		}
		return d;
	}

	template<>
	inline D3D12_DEPTH_STENCIL_VIEW_DESC DX12ViewCache::DescFromKey<ViewType::DSV>(const ViewKey& key) noexcept
	{
		D3D12_DEPTH_STENCIL_VIEW_DESC desc{};
		desc.Format = key.m_Format;
		desc.ViewDimension = static_cast<D3D12_DSV_DIMENSION>(key.m_Dimension);
		desc.Flags = static_cast<D3D12_DSV_FLAGS>(key.m_Flags);
		if (desc.ViewDimension == D3D12_DSV_DIMENSION_TEXTURE2D)
		{
			desc.Texture2D.MipSlice = key.m_MipSlice;
		}
		return desc;
	}

	template<>
	inline D3D12_SHADER_RESOURCE_VIEW_DESC DX12ViewCache::DescFromKey<ViewType::SRV>(const ViewKey& key) noexcept
	{
		D3D12_SHADER_RESOURCE_VIEW_DESC desc{};
		desc.Format = key.m_Format;
		desc.ViewDimension = static_cast<D3D12_SRV_DIMENSION>(key.m_Dimension);
		desc.Shader4ComponentMapping = key.m_ComponentMapping ? key.m_ComponentMapping : D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		if (desc.ViewDimension == D3D12_SRV_DIMENSION_TEXTURE2D)
		{
			desc.Texture2D.MostDetailedMip = key.m_MipSlice;
			desc.Texture2D.MipLevels = (key.m_MipLevels == 0) ? UINT(-1) : key.m_MipLevels;
			desc.Texture2D.PlaneSlice = key.m_PlaneSlice;
		}
		return desc;
	}

	template<>
	inline D3D12_UNORDERED_ACCESS_VIEW_DESC DX12ViewCache::DescFromKey<ViewType::UAV>(const ViewKey& key) noexcept
	{
		D3D12_UNORDERED_ACCESS_VIEW_DESC desc{};
		desc.Format = key.m_Format;
		desc.ViewDimension = static_cast<D3D12_UAV_DIMENSION>(key.m_Dimension);
		if (desc.ViewDimension == D3D12_UAV_DIMENSION_TEXTURE2D)
		{
			desc.Texture2D.MipSlice = key.m_MipSlice;
			desc.Texture2D.PlaneSlice = key.m_PlaneSlice;
		}
		return desc;
	}

#define DECL_GET_OR_CREATE_TEMPLATE_FUNC(viewType, descType)	\
	template const DX12DescriptorHandle& DX12ViewCache::GetOrCreateImpl<viewType>(ResourceIndex, DX12Texture*, std::optional<descType>) noexcept;	\
	template const DX12DescriptorHandle& DX12ViewCache::CreateFromKey<viewType>(const ViewKey&, DX12Texture*) noexcept;

	DECL_GET_OR_CREATE_TEMPLATE_FUNC(ViewType::RTV, D3D12_RENDER_TARGET_VIEW_DESC);
	DECL_GET_OR_CREATE_TEMPLATE_FUNC(ViewType::DSV, D3D12_DEPTH_STENCIL_VIEW_DESC);
	DECL_GET_OR_CREATE_TEMPLATE_FUNC(ViewType::SRV, D3D12_SHADER_RESOURCE_VIEW_DESC);
	DECL_GET_OR_CREATE_TEMPLATE_FUNC(ViewType::UAV, D3D12_UNORDERED_ACCESS_VIEW_DESC);

#undef DECL_GET_OR_CREATE_TEMPLATE_FUNC
}