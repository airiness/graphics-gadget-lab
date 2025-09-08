#pragma once
#include "DX12Descriptor.h"
#include "FNV1a.h"

namespace gglab
{
	class DX12Device;
	class DX12Texture;
	class DX12DescriptorFreeListAllocator;
	class DX12FencePoint;
	class ViewCache
	{
	public:
		using ResourceIndex = uint32_t;
		enum class Type : uint32_t
		{
			RTV,
			DSV,
			SRV,
			UVA,
			Count,
		};

		using DescriptorsAllocatorArray = std::array<DX12DescriptorFreeListAllocator*, static_cast<uint32_t>(Type::Count)>;

		struct ViewKey
		{
			Type m_Type = Type::RTV;
			DXGI_FORMAT m_Format = DXGI_FORMAT_UNKNOWN;
			uint32_t m_ResouceIndex = 0;
			uint32_t m_ComponentMapping = 0;
			uint16_t m_MipSlice = 0;
			uint16_t m_MipLevels = 0;
			uint16_t m_ArraySlice = 0;
			uint8_t m_PlaneSlice = 0;
			uint8_t m_Dimension = 0;
			uint8_t m_Flags = 0;

			bool operator==(const ViewKey& other) const noexcept = default;

			auto AsTuple() const noexcept
			{
				return std::tie(m_Type, m_Format, m_ResouceIndex, m_ComponentMapping, m_MipSlice, m_MipLevels, m_ArraySlice, m_PlaneSlice, m_Dimension, m_Flags);
			}
		};
		using ViewKeyHash = KeyHash<ViewKey>;
		
		//struct BuiltRTV
		//{
		//	ViewKey m_ViewKey;
		//	D3D12_RENDER_TARGET_VIEW_DESC m_Desc;
		//};

		//struct BuiltDSV
		//{
		//	ViewKey m_ViewKey;
		//	D3D12_DEPTH_STENCIL_VIEW_DESC m_Desc;
		//};

		//static BuiltRTV CreateRTVKey(uint32_t resourceIndex,
		//	DX12Texture* texture,
		//	const D3D12_RENDER_TARGET_VIEW_DESC& inDesc) noexcept;

		//static BuiltDSV CreateDSVKey(uint32_t resourceIndex,
		//	DX12Texture* texture,
		//	const D3D12_DEPTH_STENCIL_VIEW_DESC& inDesc) noexcept;

	public:
		explicit ViewCache(DX12Device* dx12Device, const DescriptorsAllocatorArray& descriptorAllocators) noexcept;
		GGLAB_DELETE_COPYABLE_DEFAULT_MOVABLE(ViewCache);
		~ViewCache();

		const DX12Descriptor& GetRTV(ResourceIndex resourceIndex, DX12Texture* texture,
			std::optional<D3D12_RENDER_TARGET_VIEW_DESC> desc) noexcept;

		const DX12Descriptor& GetDSV(ResourceIndex resourceIndex, DX12Texture* texture,
			std::optional<D3D12_DEPTH_STENCIL_VIEW_DESC> desc) noexcept;

		const DX12Descriptor& GetSRV(ResourceIndex resourceIndex, DX12Texture* texture,
			std::optional<D3D12_SHADER_RESOURCE_VIEW_DESC> desc) noexcept;

		const DX12Descriptor& GetUAV(ResourceIndex resourceIndex, DX12Texture* texture,
			std::optional<D3D12_UNORDERED_ACCESS_VIEW_DESC> desc) noexcept;

		void RetireResourceAllViews(ResourceIndex resourceIndex, const DX12FencePoint& fencePoint) noexcept;

		void GarbageCollect() noexcept;

		void FreeAllImmediately(ResourceIndex resourceIndex) noexcept;

	private:
		DX12DescriptorFreeListAllocator* GetDescriptorAllocator(Type type) const noexcept;

		//template<ViewCache::Type T>
		//const DX12Descriptor CreateViewImpl(uint32_t resourceIndex, DX12Texture* texture,
		//	std::optional<typename ViewTraits<T>::Desc> descOpt) noexcept
		//{
		//	using Traits = ViewTraits<T>;
		//	const typename Traits::Desc inDesc = descOpt.value_or(typename Traits::Desc{});

		//	std::scoped_lock lock(m_Mutex);

		//	auto built = Traits::Build(resourceIndex, texture, inDesc);

		//	const auto& key = built.m_ViewKey;
		//	const auto& desc = built.m_Desc;

		//	if (auto it = m_Cache.find(key); it != m_Cache.end())
		//	{
		//		return it->second;
		//	}

		//	auto* allocator = GetDescriptorAllocator(T);
		//	auto descriptor = allocator->Allocate(1);
		//	Traits::CreateView(m_DX12Device->Get(), texture->Get(), &desc, descriptor.CpuHandle());

		//	m_ResourceViews[resourceIndex].push_back(key);

		//	auto [it, inserted] = m_Cache.emplace(key, std::move(descriptor));

		//	return it->second;
		//}

	private:
		DX12Device* m_DX12Device = nullptr;
		DescriptorsAllocatorArray m_DescriptorAllocators;

		std::unordered_map<ViewKey, DX12Descriptor, ViewKeyHash> m_Cache;
		std::unordered_map<uint32_t, std::vector<ViewKey>> m_ResourceViews;

		struct Pending
		{
			uint32_t m_ResourceIndex = 0;
			DX12FencePoint m_FencePoint;
			std::vector<DX12Descriptor> m_Descriptors;
		};

		std::deque<Pending> m_Pendings;

		std::mutex m_Mutex;
	};

	template<ViewCache::Type T> struct ViewTraits;

	template<>
	struct ViewTraits<ViewCache::Type::RTV>
	{
		using Desc = D3D12_RENDER_TARGET_VIEW_DESC;
		using Built = ViewCache::BuiltRTV;

		static Built Build(uint32_t resourceIndex, DX12Texture* texture, const Desc& inDesc) noexcept;
		static void
	};



}