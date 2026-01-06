#pragma once
#include "DX12DescriptorTypes.h"
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
	};
	static constexpr uint32_t AllocatorSlotCount = 3;

	template<ViewType T> struct ViewTraits;

	struct ViewKey
	{
		DXGI_FORMAT m_Format = DXGI_FORMAT_UNKNOWN;
		ResourceIndex m_ResouceIndex;
		uint32_t m_ComponentMapping = 0;
		uint16_t m_MipSlice = 0;
		uint16_t m_MipLevels = 0;
		uint16_t m_ArraySlice = 0;
		uint8_t m_PlaneSlice = 0;
		uint8_t m_Dimension = 0;
		uint8_t m_Flags = 0;
		ViewType m_Type = ViewType::RTV;

		constexpr bool operator==(const ViewKey&) const noexcept = default;
		auto AsTuple() const noexcept
		{
			return std::make_tuple(m_Type, m_Format, m_ResouceIndex.Value(), m_ComponentMapping, m_MipSlice, m_MipLevels, m_ArraySlice, m_PlaneSlice, m_Dimension, m_Flags);
		}
	};
	using ViewKeyHash = KeyHash<ViewKey>;

	class DX12ViewCache
	{
	public:
		using DescriptorsAllocatorArray = std::array<DX12DescriptorFreeListAllocator*, AllocatorSlotCount>;

	public:
		explicit DX12ViewCache(DX12Device* dx12Device, const DescriptorsAllocatorArray& descriptorAllocators) noexcept;
		GGLAB_DELETE_COPYABLE_DEFAULT_MOVABLE(DX12ViewCache);
		~DX12ViewCache();

		const DX12DescriptorHandle& GetOrCreate(const ViewKey& key, DX12Texture* texture) noexcept;

		template<ViewType T>
		const DX12DescriptorHandle& GetOrCreate(ResourceIndex resourceIndex, DX12Texture* texture,
			std::optional<typename ViewTraits<T>::Desc> descOpt = std::nullopt) noexcept
		{
			return GetOrCreateImpl<T>(resourceIndex, texture, descOpt);
		}

		void RetireResourceAllViews(ResourceIndex resourceIndex, const DX12FencePoint& fencePoint) noexcept;
		void GarbageCollect() noexcept;
		void FreeAllImmediately(ResourceIndex resourceIndex) noexcept;

		template<ViewType T>
		static ViewKey BuildKey(ResourceIndex resourceIndex, DX12Texture* texture,
			std::optional<typename ViewTraits<T>::Desc> descOpt = std::nullopt) noexcept
		{
			using Traits = ViewTraits<T>;
			const typename Traits::Desc inDesc = descOpt.value_or(typename Traits::Desc{});
			return Traits::Build(resourceIndex, texture, inDesc).m_Key;
		}

	private:
		DX12DescriptorFreeListAllocator* GetDescriptorAllocator(ViewType type) const noexcept;

		template<ViewType T>
		const DX12DescriptorHandle& GetOrCreateImpl(ResourceIndex resourceIndex,
			DX12Texture* texture,
			std::optional<typename ViewTraits<T>::Desc> descOpt) noexcept;

		template<ViewType T>
		const DX12DescriptorHandle& CreateFromKey(const ViewKey& key, DX12Texture* texture) noexcept;

		template<ViewType T>
		static typename ViewTraits<T>::Desc DescFromKey(const ViewKey& key) noexcept;

	private:
		DX12Device* m_DX12Device = nullptr;
		DescriptorsAllocatorArray m_DescriptorAllocators;

		std::unordered_map<ViewKey, DX12DescriptorHandle, ViewKeyHash> m_Cache;
		std::unordered_map<ResourceIndex, std::vector<ViewKey>> m_ResourceViews;

		struct Pending
		{
			ResourceIndex m_ResourceIndex;
			DX12FencePoint m_FencePoint;
			std::vector<DX12DescriptorHandle> m_Descriptors;
		};

		std::deque<Pending> m_Pending;
		mutable std::shared_mutex m_Mutex;
	};

#define DEF_VIEW_TRAITS(viewType, descType)																							\
	template<>																														\
	struct ViewTraits<viewType>																										\
	{																																\
		using Desc = descType;																										\
		struct Built																												\
		{																															\
			ViewKey m_Key;																											\
			Desc m_Desc;																											\
		};																															\
		static Built Build(ResourceIndex resourceIndex, DX12Texture* texture, const Desc& inDesc) noexcept;							\
		static void Create(DX12Device* device, DX12Texture* texture, const Desc* desc, const DX12DescriptorHandle& descriptor) noexcept;	\
	};

	DEF_VIEW_TRAITS(ViewType::RTV, D3D12_RENDER_TARGET_VIEW_DESC);
	DEF_VIEW_TRAITS(ViewType::DSV, D3D12_DEPTH_STENCIL_VIEW_DESC);
	DEF_VIEW_TRAITS(ViewType::SRV, D3D12_SHADER_RESOURCE_VIEW_DESC);
	DEF_VIEW_TRAITS(ViewType::UAV, D3D12_UNORDERED_ACCESS_VIEW_DESC);

#undef DEF_VIEW_TRAITS

}