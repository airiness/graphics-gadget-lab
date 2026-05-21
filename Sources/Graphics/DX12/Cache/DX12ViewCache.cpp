#include "Core/Precompiled.h"
#include "Graphics/DX12/Cache/DX12ViewCache.h"
#include "Graphics/DX12/DX12Device.h"
#include "Graphics/DX12/DX12Texture.h"
#include "Graphics/DX12/Descriptor/DX12DescriptorManager.h"
#include "Graphics/DX12/Descriptor/DX12DescriptorFreeListAllocator.h"

namespace gglab
{
	DX12ViewCache::DX12ViewCache(const CreateInfo& createInfo) noexcept :
		m_DX12Device(createInfo.m_DX12Device),
		m_DescriptorManager(createInfo.m_DescriptorManager)
	{
		GGLAB_ASSERT(m_DX12Device);
		GGLAB_ASSERT(m_DescriptorManager);
	}

	DX12ViewCache::~DX12ViewCache()
	{
		GarbageCollect();

		// Release all pending descriptors
		for (auto& pending : m_PendingQueue)
		{
			for (auto& descriptor : pending.m_Descriptors)
			{
				if (descriptor.IsValid())
				{
					descriptor.Free();
				}
			}
		}
		m_PendingQueue.clear();

		// Release descriptor still in cache
		for (auto& descriptor : m_Cache | std::views::values)
		{
			if (descriptor.IsValid())
			{
				descriptor.Free();
			}
		}
		m_Cache.clear();

		m_ResourceViews.clear();
	}

	DX12DescriptorView DX12ViewCache::GetOrCreate(const ViewKey& key, DX12Texture* texture) noexcept
	{
		{
			std::shared_lock lock(m_Mutex);
			if (auto iterator = m_Cache.find(key); iterator != m_Cache.end())
			{
				return iterator->second.ToDescriptorView();
			}
		}

		switch (key.m_Type)
		{
		case ViewType::RTV:
		{
			return CreateFromKey<ViewType::RTV>(key, texture);
		}
		case ViewType::DSV:
		{
			return CreateFromKey<ViewType::DSV>(key, texture);
		}
		case ViewType::SRV:
		{
			return CreateFromKey<ViewType::SRV>(key, texture);
		}
		case ViewType::UAV:
		{
			return CreateFromKey<ViewType::UAV>(key, texture);
		}
		default: GGLAB_UNREACHABLE("Unknown View Type.");
		}
	}

	void DX12ViewCache::RetireResourceAllViews(ResourceIndex resourceIndex, const DX12FencePoint& fencePoint) noexcept
	{
		std::unique_lock lock(m_Mutex);

		auto iterator = m_ResourceViews.find(resourceIndex);
		if (iterator == m_ResourceViews.end())
		{
			return;
		}

		Pending pending{};
		pending.m_ResourceIndex = resourceIndex;
		pending.m_FencePoint = fencePoint;
		pending.m_Descriptors.reserve(iterator->second.size());

		for (const auto& key : iterator->second)
		{
			auto descriptorIt = m_Cache.find(key);
			if (descriptorIt != m_Cache.end())
			{
				pending.m_Descriptors.push_back(std::move(descriptorIt->second));
				m_Cache.erase(descriptorIt);
			}
		}

		m_ResourceViews.erase(iterator);

		if (!pending.m_Descriptors.empty())
		{
			m_PendingQueue.emplace_back(std::move(pending));
		}
	}

	void DX12ViewCache::GarbageCollect() noexcept
	{
		std::unique_lock lock(m_Mutex);
		while (!m_PendingQueue.empty())
		{
			auto& pending = m_PendingQueue.front();
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

			m_PendingQueue.pop_front();
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

		if (!m_PendingQueue.empty())
		{
			auto pendingIt = m_PendingQueue.begin();
			while (pendingIt != m_PendingQueue.end())
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
					pendingIt = m_PendingQueue.erase(pendingIt);
				}
				else
				{
					++pendingIt;
				}
			}
		}
	}

	template<ViewType T>
	DX12DescriptorView DX12ViewCache::GetOrCreateImpl(ResourceIndex resourceIndex, DX12Texture* texture,
		std::optional<typename ViewTraits<T>::Desc> descOpt) noexcept
	{
		using Traits = ViewTraits<T>;
		const typename Traits::Desc inDesc = descOpt.value_or(typename Traits::Desc{});

		const auto built = Traits::Build(resourceIndex, texture, inDesc);
		const auto& key = built.m_Key;

		// Check the Key has existed in cache
		{
			std::shared_lock lock(m_Mutex);
			if (auto iterator = m_Cache.find(key); iterator != m_Cache.end())
			{
				return iterator->second.ToDescriptorView();
			}
		}

		// Allocate and create
		DX12DescriptorHandle descriptor = AllocateHandle<T>();
		Traits::Create(m_DX12Device, texture, &built.m_Desc, descriptor);
		{
			std::unique_lock lock(m_Mutex);
			if (auto iterator = m_Cache.find(key); iterator != m_Cache.end())
			{
				// Other thread inserted, release it
				if (descriptor.IsValid())
				{
					descriptor.Free();
				}
				return iterator->second.ToDescriptorView();
			}

			auto [it, inserted] = m_Cache.emplace(key, std::move(descriptor));
			GGLAB_ASSERT_MSG(inserted, "ViewCache: duplicate insertion after double-checked locking");
			m_ResourceViews[resourceIndex].push_back(key);
			return it->second.ToDescriptorView();
		}
	}

	template<ViewType T>
	DX12DescriptorView DX12ViewCache::CreateFromKey(const ViewKey& key, DX12Texture* texture) noexcept
	{
		using Traits = ViewTraits<T>;
		auto desc = DescFromKey<T>(key);

		DX12DescriptorHandle descriptor = AllocateHandle<T>();
		Traits::Create(m_DX12Device, texture, &desc, descriptor);
		{
			std::unique_lock lock(m_Mutex);
			if (auto it = m_Cache.find(key); it != m_Cache.end())
			{
				if (descriptor.IsValid())
				{
					descriptor.Free();
				}
				return it->second.ToDescriptorView();
			}

			auto [it, inserted] = m_Cache.emplace(key, std::move(descriptor));
			GGLAB_ASSERT_MSG(inserted, "ViewCache: duplicate insertion after double-checked locking");
			m_ResourceViews[key.m_ResourceIndex].push_back(key);
			return it->second.ToDescriptorView();
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
			if (texDesc.SampleDesc.Count > 1)
			{
				outDesc.ViewDimension = (texDesc.DepthOrArraySize > 1) ?
					D3D12_RTV_DIMENSION_TEXTURE2DMSARRAY :
					D3D12_RTV_DIMENSION_TEXTURE2DMS;
			}
			else
			{
				outDesc.ViewDimension = (texDesc.DepthOrArraySize > 1) ?
					D3D12_RTV_DIMENSION_TEXTURE2DARRAY :
					D3D12_RTV_DIMENSION_TEXTURE2D;
			}
		}

		uint16_t mip = 0;
		uint16_t arraySlice = 0;
		uint16_t arraySize = 0;
		uint8_t plane = 0;
		if (outDesc.ViewDimension == D3D12_RTV_DIMENSION_TEXTURE2D)
		{
			mip = static_cast<uint16_t>(outDesc.Texture2D.MipSlice);
			plane = static_cast<uint8_t>(outDesc.Texture2D.PlaneSlice);
		}
		else if (outDesc.ViewDimension == D3D12_RTV_DIMENSION_TEXTURE2DARRAY)
		{
			const UINT firstSlice = outDesc.Texture2DArray.FirstArraySlice;
			UINT viewArraySize = outDesc.Texture2DArray.ArraySize;
			if (viewArraySize == 0)
			{
				viewArraySize = static_cast<UINT>(texDesc.DepthOrArraySize) - firstSlice;
				outDesc.Texture2DArray.ArraySize = viewArraySize;
			}

			mip = static_cast<uint16_t>(outDesc.Texture2DArray.MipSlice);
			arraySlice = static_cast<uint16_t>(firstSlice);
			arraySize = static_cast<uint16_t>(viewArraySize);
			plane = static_cast<uint8_t>(outDesc.Texture2DArray.PlaneSlice);
		}
		else if (outDesc.ViewDimension == D3D12_RTV_DIMENSION_TEXTURE2DMSARRAY)
		{
			const UINT firstSlice = outDesc.Texture2DMSArray.FirstArraySlice;
			UINT viewArraySize = outDesc.Texture2DMSArray.ArraySize;
			if (viewArraySize == 0)
			{
				viewArraySize = static_cast<UINT>(texDesc.DepthOrArraySize) - firstSlice;
				outDesc.Texture2DMSArray.ArraySize = viewArraySize;
			}

			arraySlice = static_cast<uint16_t>(firstSlice);
			arraySize = static_cast<uint16_t>(viewArraySize);
		}

		auto& key = built.m_Key;
		key.m_Type = ViewType::RTV;
		key.m_ResourceIndex = resourceIndex;
		key.m_Format = outDesc.Format;
		key.m_MipSlice = mip;
		key.m_ArraySlice = arraySlice;
		key.m_ArraySize = arraySize;
		key.m_PlaneSlice = plane;
		key.m_Dimension = static_cast<uint8_t>(outDesc.ViewDimension);
		key.m_Flags = 0;
		key.m_ComponentMapping = 0;
		key.m_MipLevels = 0;
		key.m_ResourceMinLODClamp = 0.0f;

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
		const auto encodeDsvFlags = [](D3D12_DSV_FLAGS flags) noexcept -> uint8_t
			{
				using U = std::underlying_type_t<D3D12_DSV_FLAGS>;
				constexpr uint8_t kBits = static_cast<uint8_t>(
					static_cast<U>(D3D12_DSV_FLAG_READ_ONLY_DEPTH) |
					static_cast<U>(D3D12_DSV_FLAG_READ_ONLY_STENCIL));

				return static_cast<uint8_t>(static_cast<U>(flags)) & kBits;
			};

		auto& key = built.m_Key;
		key.m_Type = ViewType::DSV;
		key.m_ResourceIndex = resourceIndex;
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
		uint16_t arraySlice = 0;
		uint16_t arraySize = 0;
		uint8_t plane = 0;
		float minLodClamp = 0.0f;
		if (outDesc.ViewDimension == D3D12_SRV_DIMENSION_TEXTURE2D)
		{
			if (outDesc.Texture2D.MipLevels == ViewKey::UnspecifiedMipLevelsInDesc)
			{
				outDesc.Texture2D.MipLevels = ViewKey::AllRemainingMipLevelsD3D12;
			}

			most = static_cast<uint16_t>(outDesc.Texture2D.MostDetailedMip);
			plane = static_cast<uint8_t>(outDesc.Texture2D.PlaneSlice);
			levels = ViewKey::EncodeMipLevels(outDesc.Texture2D.MipLevels);
			minLodClamp = outDesc.Texture2D.ResourceMinLODClamp;
		}
		else if (outDesc.ViewDimension == D3D12_SRV_DIMENSION_TEXTURE2DARRAY)
		{
			const UINT firstSlice = outDesc.Texture2DArray.FirstArraySlice;
			UINT viewArraySize = outDesc.Texture2DArray.ArraySize;
			if (viewArraySize == 0)
			{
				viewArraySize = static_cast<UINT>(texDesc.DepthOrArraySize) - firstSlice;
				outDesc.Texture2DArray.ArraySize = viewArraySize;
			}
			if (outDesc.Texture2DArray.MipLevels == ViewKey::UnspecifiedMipLevelsInDesc)
			{
				outDesc.Texture2DArray.MipLevels = ViewKey::AllRemainingMipLevelsD3D12;
			}

			most = static_cast<uint16_t>(outDesc.Texture2DArray.MostDetailedMip);
			levels = ViewKey::EncodeMipLevels(outDesc.Texture2DArray.MipLevels);
			arraySlice = static_cast<uint16_t>(firstSlice);
			arraySize = static_cast<uint16_t>(viewArraySize);
			plane = static_cast<uint8_t>(outDesc.Texture2DArray.PlaneSlice);
			minLodClamp = outDesc.Texture2DArray.ResourceMinLODClamp;
		}
		else if (outDesc.ViewDimension == D3D12_SRV_DIMENSION_TEXTURECUBE)
		{
			if (outDesc.TextureCube.MipLevels == ViewKey::UnspecifiedMipLevelsInDesc)
			{
				outDesc.TextureCube.MipLevels = ViewKey::AllRemainingMipLevelsD3D12;
			}

			most = static_cast<uint16_t>(outDesc.TextureCube.MostDetailedMip);
			levels = ViewKey::EncodeMipLevels(outDesc.TextureCube.MipLevels);
			minLodClamp = outDesc.TextureCube.ResourceMinLODClamp;
		}
		else if (outDesc.ViewDimension == D3D12_SRV_DIMENSION_TEXTURECUBEARRAY)
		{
			const UINT firstFace = outDesc.TextureCubeArray.First2DArrayFace;
			UINT numCubes = outDesc.TextureCubeArray.NumCubes;
			if (numCubes == 0)
			{
				numCubes = (static_cast<UINT>(texDesc.DepthOrArraySize) - firstFace) / CubemapFaceCount;
				outDesc.TextureCubeArray.NumCubes = numCubes;
			}
			if (outDesc.TextureCubeArray.MipLevels == ViewKey::UnspecifiedMipLevelsInDesc)
			{
				outDesc.TextureCubeArray.MipLevels = ViewKey::AllRemainingMipLevelsD3D12;
			}

			most = static_cast<uint16_t>(outDesc.TextureCubeArray.MostDetailedMip);
			levels = ViewKey::EncodeMipLevels(outDesc.TextureCubeArray.MipLevels);
			arraySlice = static_cast<uint16_t>(firstFace);
			arraySize = static_cast<uint16_t>(numCubes);
			minLodClamp = outDesc.TextureCubeArray.ResourceMinLODClamp;
		}

		auto& key = built.m_Key;
		key.m_Type = ViewType::SRV;
		key.m_ResourceIndex = resourceIndex;
		key.m_Format = outDesc.Format;
		key.m_MipSlice = most;
		key.m_MipLevels = levels;
		key.m_ArraySlice = arraySlice;
		key.m_ArraySize = arraySize;
		key.m_PlaneSlice = plane;
		key.m_Dimension = static_cast<uint8_t>(outDesc.ViewDimension);
		key.m_Flags = 0;
		key.m_ComponentMapping = outDesc.Shader4ComponentMapping;
		key.m_ResourceMinLODClamp = minLodClamp;

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
		key.m_ResourceIndex = resourceIndex;
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
		else if (d.ViewDimension == D3D12_RTV_DIMENSION_TEXTURE2DARRAY)
		{
			d.Texture2DArray.MipSlice = key.m_MipSlice;
			d.Texture2DArray.FirstArraySlice = key.m_ArraySlice;
			d.Texture2DArray.ArraySize = key.m_ArraySize;
			d.Texture2DArray.PlaneSlice = key.m_PlaneSlice;
		}
		else if (d.ViewDimension == D3D12_RTV_DIMENSION_TEXTURE2DMSARRAY)
		{
			d.Texture2DMSArray.FirstArraySlice = key.m_ArraySlice;
			d.Texture2DMSArray.ArraySize = key.m_ArraySize;
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
			desc.Texture2D.MipLevels = ViewKey::DecodeMipLevels(key.m_MipLevels);
			desc.Texture2D.PlaneSlice = key.m_PlaneSlice;
			desc.Texture2D.ResourceMinLODClamp = key.m_ResourceMinLODClamp;
		}
		else if (desc.ViewDimension == D3D12_SRV_DIMENSION_TEXTURE2DARRAY)
		{
			desc.Texture2DArray.MostDetailedMip = key.m_MipSlice;
			desc.Texture2DArray.MipLevels = ViewKey::DecodeMipLevels(key.m_MipLevels);
			desc.Texture2DArray.FirstArraySlice = key.m_ArraySlice;
			desc.Texture2DArray.ArraySize = key.m_ArraySize;
			desc.Texture2DArray.PlaneSlice = key.m_PlaneSlice;
			desc.Texture2DArray.ResourceMinLODClamp = key.m_ResourceMinLODClamp;
		}
		else if (desc.ViewDimension == D3D12_SRV_DIMENSION_TEXTURECUBE)
		{
			desc.TextureCube.MostDetailedMip = key.m_MipSlice;
			desc.TextureCube.MipLevels = ViewKey::DecodeMipLevels(key.m_MipLevels);
			desc.TextureCube.ResourceMinLODClamp = key.m_ResourceMinLODClamp;
		}
		else if (desc.ViewDimension == D3D12_SRV_DIMENSION_TEXTURECUBEARRAY)
		{
			desc.TextureCubeArray.MostDetailedMip = key.m_MipSlice;
			desc.TextureCubeArray.MipLevels = ViewKey::DecodeMipLevels(key.m_MipLevels);
			desc.TextureCubeArray.First2DArrayFace = key.m_ArraySlice;
			desc.TextureCubeArray.NumCubes = key.m_ArraySize;
			desc.TextureCubeArray.ResourceMinLODClamp = key.m_ResourceMinLODClamp;
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
	template DX12DescriptorView DX12ViewCache::GetOrCreateImpl<viewType>(ResourceIndex, DX12Texture*, std::optional<descType>) noexcept;	\
	template DX12DescriptorView DX12ViewCache::CreateFromKey<viewType>(const ViewKey&, DX12Texture*) noexcept;

	DECL_GET_OR_CREATE_TEMPLATE_FUNC(ViewType::RTV, D3D12_RENDER_TARGET_VIEW_DESC);
	DECL_GET_OR_CREATE_TEMPLATE_FUNC(ViewType::DSV, D3D12_DEPTH_STENCIL_VIEW_DESC);
	DECL_GET_OR_CREATE_TEMPLATE_FUNC(ViewType::SRV, D3D12_SHADER_RESOURCE_VIEW_DESC);
	DECL_GET_OR_CREATE_TEMPLATE_FUNC(ViewType::UAV, D3D12_UNORDERED_ACCESS_VIEW_DESC);

#undef DECL_GET_OR_CREATE_TEMPLATE_FUNC
}
