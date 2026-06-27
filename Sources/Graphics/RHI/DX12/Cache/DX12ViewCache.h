#pragma once
#include "Graphics/RHI/DX12/Descriptor/DX12DescriptorManager.h"
#include "Graphics/RHI/RHIDescriptor.h"
#include "Graphics/RHI/RHIFence.h"
#include "Graphics/RHI/RHIHandleTable.h"
#include "Graphics/RHI/RHISampler.h"
#include "Graphics/GraphicsTypes.h"
#include "Core/Hash/FNV1a.h"

#include <span>

namespace gglab
{
class DX12Device;
class DX12Buffer;
class DX12Texture;
class DX12DescriptorManager;
class DX12FencePoint;

	enum class ViewType : uint8_t
	{
		RTV,
		DSV,
		SRV,
		UAV,
	};

	template<ViewType T> struct ViewTraits;

	struct ViewKey
	{
		static constexpr uint16_t AllRemainingMipLevelsEncoded = 0;
		static constexpr UINT UnspecifiedMipLevelsInDesc = 0;
		static constexpr UINT AllRemainingMipLevelsD3D12 = static_cast<UINT>(-1);

		DXGI_FORMAT m_Format = DXGI_FORMAT_UNKNOWN;
		ResourceIndex m_ResourceIndex;
		uint32_t m_ComponentMapping = 0;
		uint16_t m_MipSlice = 0;
		uint16_t m_MipLevels = 0;
		uint16_t m_ArraySlice = 0;
		uint16_t m_ArraySize = 0;
		float m_ResourceMinLODClamp = 0.0f;
		uint8_t m_PlaneSlice = 0;
		uint8_t m_Dimension = 0;
		uint8_t m_Flags = 0;
		ViewType m_Type = ViewType::RTV;

		constexpr bool operator==(const ViewKey&) const noexcept = default;

		static constexpr uint16_t EncodeMipLevels(UINT mipLevels) noexcept
		{
			return (mipLevels == AllRemainingMipLevelsD3D12) ?
				AllRemainingMipLevelsEncoded :
				static_cast<uint16_t>(mipLevels);
		}

		static constexpr UINT DecodeMipLevels(uint16_t mipLevels) noexcept
		{
			return (mipLevels == AllRemainingMipLevelsEncoded) ?
				AllRemainingMipLevelsD3D12 :
				mipLevels;
		}

		auto AsTuple() const noexcept
		{
			return std::make_tuple(
				m_Type,
				m_Format,
				m_ResourceIndex.Value(),
				m_ComponentMapping,
				m_MipSlice,
				m_MipLevels,
				m_ArraySlice,
				m_ArraySize,
				m_ResourceMinLODClamp,
				m_PlaneSlice,
				m_Dimension,
				m_Flags);
		}
	};
	using ViewKeyHash = KeyHash<ViewKey>;

	class DX12ViewCache
	{
	public:
		struct CreateInfo
		{
			DX12Device* m_DX12Device = nullptr;
			DX12DescriptorManager* m_DescriptorManager = nullptr;
		};

	public:
		explicit DX12ViewCache(const CreateInfo& createInfo) noexcept;
		GGLAB_DELETE_COPYABLE_DEFAULT_MOVABLE(DX12ViewCache);
		~DX12ViewCache();

		DX12DescriptorView GetOrCreate(const ViewKey& key, DX12Texture* texture) noexcept;
		RHITextureViewHandle GetOrCreateTextureView(
			RHITextureHandle texture,
			const RHITextureViewDesc& desc) noexcept;
		RHIBufferViewHandle GetOrCreateBufferView(
			RHIBufferHandle buffer,
			const RHIBufferViewDesc& desc) noexcept;
		RHISamplerHandle GetOrCreateSampler(const RHISamplerDesc& desc) noexcept;
		DX12DescriptorView ResolveTextureView(RHITextureViewHandle view) const noexcept;
		DX12DescriptorView ResolveBufferView(RHIBufferViewHandle view) const noexcept;
		RHIDescriptorHandle ResolveTextureViewDescriptor(RHITextureViewHandle view) const noexcept;
		RHIDescriptorHandle ResolveBufferViewDescriptor(RHIBufferViewHandle view) const noexcept;
		RHIDescriptorHandle ResolveSamplerDescriptor(RHISamplerHandle sampler) const noexcept;
		void DestroyTextureView(RHITextureViewHandle view) noexcept;
		void DestroyBufferView(RHIBufferViewHandle view) noexcept;
		void DestroySampler(RHISamplerHandle sampler) noexcept;
		bool IsSamplerAlive(RHISamplerHandle sampler) const noexcept;
		DX12DescriptorManager* GetDescriptorManager() const noexcept { return m_DescriptorManager; }

		template<ViewType T>
		DX12DescriptorView GetOrCreate(ResourceIndex resourceIndex, DX12Texture* texture,
			std::optional<typename ViewTraits<T>::Desc> descOpt = std::nullopt) noexcept
		{
			return GetOrCreateImpl<T>(resourceIndex, texture, descOpt);
		}

		void RetireResourceAllViews(ResourceIndex resourceIndex, const DX12FencePoint& fencePoint) noexcept;
		void RetireTextureViews(
			RHITextureHandle texture,
			std::span<const RHIFencePoint> fencePoints) noexcept;
		void RetireBufferViews(
			RHIBufferHandle buffer,
			std::span<const RHIFencePoint> fencePoints) noexcept;
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
		template<ViewType T>
		DX12DescriptorHandle AllocateHandle() const noexcept
		{
			constexpr auto allocatorType = ViewTraits<T>::AllocatorType;
			auto* allocator = m_DescriptorManager->GetFreeListAllocator(allocatorType);
			return allocator->AllocateHandle();
		}

		template<ViewType T>
		DX12DescriptorView GetOrCreateImpl(ResourceIndex resourceIndex,
			DX12Texture* texture,
			std::optional<typename ViewTraits<T>::Desc> descOpt) noexcept;

		template<ViewType T>
		DX12DescriptorView CreateFromKey(const ViewKey& key, DX12Texture* texture) noexcept;

		template<ViewType T>
		static typename ViewTraits<T>::Desc DescFromKey(const ViewKey&) noexcept;
		DX12DescriptorHandle CreateTextureDescriptor(
			const RHITextureViewKey& key,
			DX12Texture* texture) const noexcept;
		DX12DescriptorHandle CreateBufferDescriptor(
			const RHIBufferViewKey& key,
			DX12Buffer* buffer) const noexcept;
		DX12DescriptorHandle CreateSamplerDescriptor(const RHISamplerDesc& desc) const noexcept;
		void DestroyTextureViewLocked(RHITextureViewHandle view) noexcept;
		void DestroyBufferViewLocked(RHIBufferViewHandle view) noexcept;
		void DestroySamplerLocked(RHISamplerHandle sampler) noexcept;

	private:
		DX12Device* m_DX12Device = nullptr;
		DX12DescriptorManager* m_DescriptorManager = nullptr;

		template<typename HandleT, typename KeyT>
		struct ViewSlot
		{
			typename HandleT::GenerationType m_Generation = 1;
			RHIHandleSlotState m_State = RHIHandleSlotState::Free;
			KeyT m_Key;
			std::vector<RHIFencePoint> m_RetirementPoints;
			DX12DescriptorHandle m_Descriptor;
		};

		using TextureViewSlot = ViewSlot<RHITextureViewHandle, RHITextureViewKey>;
		using BufferViewSlot = ViewSlot<RHIBufferViewHandle, RHIBufferViewKey>;
		using SamplerSlot = ViewSlot<RHISamplerHandle, RHISamplerDesc>;

		RHIHandleTable<RHITextureViewHandle, TextureViewSlot> m_RHITextureViews;
		RHIHandleTable<RHIBufferViewHandle, BufferViewSlot> m_RHIBufferViews;
		RHIHandleTable<RHISamplerHandle, SamplerSlot> m_RHISamplers;
		std::unordered_map<RHITextureViewKey, RHITextureViewHandle, RHITextureViewKeyHash> m_RHITextureViewCache;
		std::unordered_map<RHIBufferViewKey, RHIBufferViewHandle, RHIBufferViewKeyHash> m_RHIBufferViewCache;
		std::unordered_map<RHISamplerDesc, RHISamplerHandle, KeyHash<RHISamplerDesc>> m_RHISamplerCache;
		std::unordered_map<RHITextureHandle, std::vector<RHITextureViewHandle>> m_RHITextureResourceViews;
		std::unordered_map<RHIBufferHandle, std::vector<RHIBufferViewHandle>> m_RHIBufferResourceViews;

		std::unordered_map<ViewKey, DX12DescriptorHandle, ViewKeyHash> m_Cache;
		std::unordered_map<ResourceIndex, std::vector<ViewKey>> m_ResourceViews;

		struct Pending
		{
			ResourceIndex m_ResourceIndex;
			DX12FencePoint m_FencePoint;
			std::vector<DX12DescriptorHandle> m_Descriptors;
		};

		std::deque<Pending> m_PendingQueue;
		mutable std::shared_mutex m_Mutex;
	};

#define DEF_VIEW_TRAITS(viewType, descType, allocatorType)																					\
	template<>																																\
	struct ViewTraits<viewType>																												\
	{																																		\
		using Desc = descType;																												\
		struct Built																														\
		{																																	\
			ViewKey m_Key;																													\
			Desc m_Desc;																													\
		};																																	\
		static Built Build(ResourceIndex resourceIndex, DX12Texture* texture, const Desc& inDesc) noexcept;									\
		static void Create(DX12Device* device, DX12Texture* texture, const Desc* desc, const DX12DescriptorHandle& descriptor) noexcept;	\
		static constexpr DX12DescriptorManager::AllocatorType AllocatorType = (allocatorType);										\
	};

	DEF_VIEW_TRAITS(ViewType::RTV, D3D12_RENDER_TARGET_VIEW_DESC, DX12DescriptorManager::AllocatorType::GeneralRtv);
	DEF_VIEW_TRAITS(ViewType::DSV, D3D12_DEPTH_STENCIL_VIEW_DESC, DX12DescriptorManager::AllocatorType::GeneralDsv);
	DEF_VIEW_TRAITS(ViewType::SRV, D3D12_SHADER_RESOURCE_VIEW_DESC, DX12DescriptorManager::AllocatorType::GeneralCbvSrvUav);
	DEF_VIEW_TRAITS(ViewType::UAV, D3D12_UNORDERED_ACCESS_VIEW_DESC, DX12DescriptorManager::AllocatorType::GeneralCbvSrvUav);

#undef DEF_VIEW_TRAITS

}
