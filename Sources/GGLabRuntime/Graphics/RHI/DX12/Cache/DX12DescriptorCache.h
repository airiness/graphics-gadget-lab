#pragma once
#include "GGLabFoundation/Base/CoreMacros.h"
#include "Core/Hash/KeyHash.h"
#include "Graphics/RHI/DX12/Descriptor/DX12DescriptorManager.h"
#include "Graphics/RHI/RHIDescriptor.h"
#include "Graphics/RHI/RHIFence.h"
#include "Graphics/RHI/RHIHandleTable.h"
#include "Graphics/RHI/RHISampler.h"

#include <shared_mutex>
#include <span>
#include <unordered_map>
#include <vector>

namespace gglab
{
	class DX12Device;
	class DX12Buffer;
	class DX12Texture;

	class DX12DescriptorCache
	{
	public:
		struct CreateInfo
		{
			DX12Device* m_DX12Device = nullptr;
			DX12DescriptorManager* m_DescriptorManager = nullptr;
		};

	public:
		explicit DX12DescriptorCache(const CreateInfo& createInfo) noexcept;
		GGLAB_DELETE_COPYABLE_DEFAULT_MOVABLE(DX12DescriptorCache);
		~DX12DescriptorCache();

		RHITextureViewHandle GetOrCreateTextureView(
			RHITextureHandle texture, const RHITextureViewDesc& desc) noexcept;
		RHIBufferViewHandle GetOrCreateBufferView(
			RHIBufferHandle buffer, const RHIBufferViewDesc& desc) noexcept;
		RHISamplerHandle GetOrCreateSampler(const RHISamplerDesc& desc) noexcept;
		DX12DescriptorView ResolveTextureView(RHITextureViewHandle view) const noexcept;
		bool ResolveTextureViewInfo(RHITextureViewHandle view, DX12DescriptorView& descriptor,
			RHITextureViewKey& key) const noexcept;
		DX12DescriptorView ResolveBufferView(RHIBufferViewHandle view) const noexcept;
		RHIDescriptorHandle ResolveTextureViewDescriptor(RHITextureViewHandle view) const noexcept;
		RHIDescriptorHandle ResolveBufferViewDescriptor(RHIBufferViewHandle view) const noexcept;
		RHIDescriptorHandle ResolveSamplerDescriptor(RHISamplerHandle sampler) const noexcept;
		void DestroyTextureView(RHITextureViewHandle view) noexcept;
		void DestroyBufferView(RHIBufferViewHandle view) noexcept;
		void DestroySampler(RHISamplerHandle sampler) noexcept;
		bool IsSamplerAlive(RHISamplerHandle sampler) const noexcept;
		DX12DescriptorManager* GetDescriptorManager() const noexcept { return m_DescriptorManager; }

		void RetireTextureViews(
			RHITextureHandle texture, std::span<const RHIFencePoint> fencePoints) noexcept;
		void RetireBufferViews(
			RHIBufferHandle buffer, std::span<const RHIFencePoint> fencePoints) noexcept;
		void GarbageCollect() noexcept;

	private:
		DX12DescriptorHandle AllocateHandle(
			DX12DescriptorManager::AllocatorType allocatorType) const noexcept;
		DX12DescriptorHandle CreateTextureDescriptor(
			const RHITextureViewKey& key, DX12Texture* texture) const noexcept;
		DX12DescriptorHandle CreateBufferDescriptor(
			const RHIBufferViewKey& key, DX12Buffer* buffer) const noexcept;
		DX12DescriptorHandle CreateSamplerDescriptor(const RHISamplerDesc& desc) const noexcept;
		void DestroyTextureViewLocked(RHITextureViewHandle view) noexcept;
		void DestroyBufferViewLocked(RHIBufferViewHandle view) noexcept;
		void DestroySamplerLocked(RHISamplerHandle sampler) noexcept;

	private:
		DX12Device* m_DX12Device = nullptr;
		DX12DescriptorManager* m_DescriptorManager = nullptr;

		template <typename HandleT, typename KeyT> struct ViewSlot
		{
			typename HandleT::GenerationType m_Generation = 1;
			RHIHandleSlotState m_State = RHIHandleSlotState::Free;
			KeyT m_Key;
			std::vector<RHIFencePoint> m_RetirementPoints;
			DX12DescriptorHandle m_Descriptor;
		};

		using TextureViewSlot = ViewSlot<RHITextureViewHandle, RHITextureViewKey>;
		using BufferViewSlot = ViewSlot<RHIBufferViewHandle, RHIBufferViewKey>;

		struct SamplerSlot
		{
			RHISamplerHandle::GenerationType m_Generation = 1;
			RHIHandleSlotState m_State = RHIHandleSlotState::Free;
			RHISamplerDesc m_Key{};
			std::vector<RHIFencePoint> m_RetirementPoints;
			DX12DescriptorHandle m_Descriptor;
			uint32_t m_OwnerCount = 0;
		};

		RHIHandleTable<RHITextureViewHandle, TextureViewSlot> m_RHITextureViews;
		RHIHandleTable<RHIBufferViewHandle, BufferViewSlot> m_RHIBufferViews;
		RHIHandleTable<RHISamplerHandle, SamplerSlot> m_RHISamplers;
		std::unordered_map<RHITextureViewKey, RHITextureViewHandle, RHITextureViewKeyHash>
			m_RHITextureViewCache;
		std::unordered_map<RHIBufferViewKey, RHIBufferViewHandle, RHIBufferViewKeyHash>
			m_RHIBufferViewCache;
		std::unordered_map<RHISamplerDesc, RHISamplerHandle, KeyHash<RHISamplerDesc>>
			m_RHISamplerCache;
		std::unordered_map<RHITextureHandle, std::vector<RHITextureViewHandle>>
			m_RHITextureResourceViews;
		std::unordered_map<RHIBufferHandle, std::vector<RHIBufferViewHandle>>
			m_RHIBufferResourceViews;

		mutable std::shared_mutex m_Mutex;
	};

}
