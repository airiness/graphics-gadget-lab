#pragma once
#include "DX12Descriptor.h"
#include "GraphicsTypes.h"
#include "FNV1a.h"

namespace gglab
{
	class DX12Device;
	class DX12Texture;
	class DX12DescriptorFreeListAllocator;
	class DX12FencePoint;

	enum class ViewType : uint8_t
	{
		RTV,
		DSV,
		SRV,
		UAV,
		Count = UAV,
	};

	template<ViewType T> struct ViewTraits;

	class ViewCache
	{
	public:
		using DescriptorsAllocatorArray = std::array<DX12DescriptorFreeListAllocator*, static_cast<uint32_t>(ViewType::Count)>;

		struct ViewKey
		{
			DXGI_FORMAT m_Format = DXGI_FORMAT_UNKNOWN;
			uint32_t m_ResouceIndexValue;
			uint32_t m_ComponentMapping = 0;
			uint16_t m_MipSlice = 0;
			uint16_t m_MipLevels = 0;
			uint16_t m_ArraySlice = 0;
			uint8_t m_PlaneSlice = 0;
			uint8_t m_Dimension = 0;
			uint8_t m_Flags = 0;
			ViewType m_Type = ViewType::RTV;

			bool operator==(const ViewKey& other) const noexcept = default;

			auto AsTuple() const noexcept
			{
				return std::tie(m_Type, m_Format, m_ResouceIndexValue, m_ComponentMapping, m_MipSlice, m_MipLevels, m_ArraySlice, m_PlaneSlice, m_Dimension, m_Flags);
			}
		};
		using ViewKeyHash = KeyHash<ViewKey>;

	public:
		explicit ViewCache(DX12Device* dx12Device, const DescriptorsAllocatorArray& descriptorAllocators) noexcept;
		GGLAB_DELETE_COPYABLE_DEFAULT_MOVABLE(ViewCache);
		~ViewCache();

		const DX12Descriptor& GetRTV(ResourceIndex resourceIndex, DX12Texture* texture,
			std::optional<D3D12_RENDER_TARGET_VIEW_DESC> descOpt) noexcept;

		const DX12Descriptor& GetDSV(ResourceIndex resourceIndex, DX12Texture* texture,
			std::optional<D3D12_DEPTH_STENCIL_VIEW_DESC> descOpt) noexcept;

		const DX12Descriptor& GetSRV(ResourceIndex resourceIndex, DX12Texture* texture,
			std::optional<D3D12_SHADER_RESOURCE_VIEW_DESC> descOpt) noexcept;

		const DX12Descriptor& GetUAV(ResourceIndex resourceIndex, DX12Texture* texture,
			std::optional<D3D12_UNORDERED_ACCESS_VIEW_DESC> descOpt) noexcept;

		void RetireResourceAllViews(ResourceIndex resourceIndex, const DX12FencePoint& fencePoint) noexcept;

		void GarbageCollect() noexcept;

		void FreeAllImmediately(ResourceIndex resourceIndex) noexcept;

	private:
		DX12DescriptorFreeListAllocator* GetDescriptorAllocator(ViewType type) const noexcept;

		template<ViewType T>
		const DX12Descriptor& GetOrCreateImpl(ResourceIndex resourceIndex,
			DX12Texture* texture,
			std::optional<typename ViewTraits<T>::Desc> descOpt) noexcept;

	private:
		DX12Device* m_DX12Device = nullptr;
		DescriptorsAllocatorArray m_DescriptorAllocators;

		std::unordered_map<ViewKey, DX12Descriptor, ViewKeyHash> m_Cache;
		std::unordered_map<ResourceIndex, std::vector<ViewKey>> m_ResourceViews;

		struct Pending
		{
			ResourceIndex m_ResourceIndex;
			DX12FencePoint m_FencePoint;
			std::vector<DX12Descriptor> m_Descriptors;
		};

		std::deque<Pending> m_Pendings;

		std::mutex m_Mutex;
	};

#define DEF_VIEW_TRAITS(viewType, descType)																							\
	template<>																														\
	struct ViewTraits<viewType>																										\
	{																																\
		using Desc = descType;																										\
		struct Built																												\
		{																															\
			ViewCache::ViewKey m_Key;																								\
			Desc m_Desc;																											\
		};																															\
		static Built Build(ResourceIndex resourceIndex, DX12Texture* texture, const Desc& inDesc) noexcept;							\
		static void Create(DX12Device* device, DX12Texture* texture, const Desc* desc, const DX12Descriptor& descriptor) noexcept;	\
	};

	DEF_VIEW_TRAITS(ViewType::RTV, D3D12_RENDER_TARGET_VIEW_DESC);
	DEF_VIEW_TRAITS(ViewType::DSV, D3D12_DEPTH_STENCIL_VIEW_DESC);
	DEF_VIEW_TRAITS(ViewType::SRV, D3D12_SHADER_RESOURCE_VIEW_DESC);
	DEF_VIEW_TRAITS(ViewType::UAV, D3D12_UNORDERED_ACCESS_VIEW_DESC);

#undef DEF_VIEW_TRAITS

}