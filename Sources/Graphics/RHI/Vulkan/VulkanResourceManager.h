#pragma once
#include "Core/CoreMacros.h"
#include "Graphics/RHI/RHIBuffer.h"
#include "Graphics/RHI/RHIDescriptor.h"
#include "Graphics/RHI/RHIFence.h"
#include "Graphics/RHI/RHIHandleTable.h"
#include "Graphics/RHI/RHIResourceDebug.h"
#include "Graphics/RHI/RHISampler.h"
#include "Graphics/RHI/RHITexture.h"
#include "Graphics/RHI/Vulkan/VulkanDescriptorManager.h"
#include "Graphics/RHI/Vulkan/VulkanFormat.h"
#include "Graphics/RHI/Vulkan/VulkanResource.h"

#include <vulkan/vulkan.h>

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace gglab
{
	class VulkanDevice;

	// Owns GGLab resource handles, native Vulkan objects, views, samplers,
	// debug identity, last-use tracking and deferred destruction. Descriptor
	// publication may retain shared view and parent-resource backing until
	// every joined retirement gate completes.
	class VulkanResourceManager
	{
	public:
		struct TextureViewBinding
		{
			RHITextureHandle m_Texture{};
			RHITextureViewDesc m_ViewDesc{};
			RHITextureDesc m_TextureDesc{};
			VkImage m_Image = VK_NULL_HANDLE;
			VkImageView m_ImageView = VK_NULL_HANDLE;
			RHIExtent3D m_Extent{};
		};

		struct ImportedTextureDesc
		{
			RHIImportedTextureDesc m_RHI;
			VkImage m_Image = VK_NULL_HANDLE;
			RHIResourceDebugIdentityDesc m_DebugIdentity;
		};

		struct ImportedBufferDesc
		{
			RHIImportedBufferDesc m_RHI;
			VkBuffer m_Buffer = VK_NULL_HANDLE;
			RHIResourceDebugIdentityDesc m_DebugIdentity;
		};

	private:
		struct Diagnostics
		{
			uint64_t m_TextureCreateCount = 0;
			uint64_t m_BufferCreateCount = 0;
			uint64_t m_TextureImportCount = 0;
			uint64_t m_BufferImportCount = 0;
			uint64_t m_TextureRetireCount = 0;
			uint64_t m_BufferRetireCount = 0;
			uint64_t m_CreateFailureCount = 0;
			uint64_t m_ImportFailureCount = 0;
			uint64_t m_InvalidUseCount = 0;
			uint64_t m_InvalidDestroyCount = 0;
			uint64_t m_StaleDestroyCount = 0;
			uint64_t m_DoubleDestroyCount = 0;
		};

		// One native view created on a parent texture. Only shader-visible
		// views (ShaderResource/UnorderedAccess) hold a descriptor index;
		// attachment views have no bindless index.
		struct TextureViewSlot
		{
			RHITextureViewHandle::GenerationType m_Generation = 1;
			RHIHandleSlotState m_State = RHIHandleSlotState::Free;
			RHITextureViewKey m_Key{};
			std::vector<RHIFencePoint> m_RetirementPoints;
			VkImage m_ParentImage = VK_NULL_HANDLE;
			std::optional<uint32_t> m_DescriptorIndex;
			std::shared_ptr<VulkanDescriptorBacking> m_Backing;
		};

		// A buffer view. The native VkBufferView is only created for texel views
		// (non-Unknown format); plain buffer descriptors are consumed
		// directly by the descriptor layer. Buffer views never consume the
		// bindless image arena.
		struct BufferViewSlot
		{
			RHIBufferViewHandle::GenerationType m_Generation = 1;
			RHIHandleSlotState m_State = RHIHandleSlotState::Free;
			RHIBufferViewKey m_Key{};
			std::vector<RHIFencePoint> m_RetirementPoints;
			VkBuffer m_ParentBuffer = VK_NULL_HANDLE;
			std::shared_ptr<VulkanDescriptorBacking> m_Backing;
		};

		struct SamplerSlot
		{
			RHISamplerHandle::GenerationType m_Generation = 1;
			RHIHandleSlotState m_State = RHIHandleSlotState::Free;
			RHISamplerDesc m_Desc{};
			std::vector<RHIFencePoint> m_RetirementPoints;
			std::optional<uint32_t> m_DescriptorIndex;
			std::shared_ptr<VulkanDescriptorBacking> m_Backing;
		};

		template <typename HandleT, typename ResourceT> struct ResourceSlot
		{
			typename HandleT::GenerationType m_Generation = 1;
			RHIResourceOwnership m_Ownership = RHIResourceOwnership::Owned;
			RHIHandleSlotState m_State = RHIHandleSlotState::Free;
			RHIResourceDebugIdentity m_DebugIdentity;
			RHIResourceDebugBinding m_DebugBinding;
			std::vector<RHIResourceDebugBinding> m_DebugBindingHistory;
			std::string m_DebugName;
			std::vector<RHIFencePoint> m_LastUsePoints;
			std::vector<RHIFencePoint> m_RetirementPoints;
			std::shared_ptr<ResourceT> m_Resource;
		};

		// Texture/buffer slot records carry the RHI description so view
		// creation can validate against the exact resource contract.
		struct TextureSlot : ResourceSlot<RHITextureHandle, VulkanTexture>
		{
			RHITextureDesc m_RHIDesc{};
		};
		struct BufferSlot : ResourceSlot<RHIBufferHandle, VulkanBuffer>
		{
			RHIBufferDesc m_RHIDesc{};
		};

	public:
		VulkanResourceManager() noexcept = default;
		GGLAB_DELETE_COPYABLE_MOVABLE(VulkanResourceManager);
		~VulkanResourceManager() noexcept;

		void Initialize(VulkanDevice* device) noexcept;
		void Finalize() noexcept;

		RHITextureHandle CreateTexture(const RHIOwnedTextureCreateInfo& createInfo,
			const RHIResourceDebugIdentityDesc& debugIdentity = {}) noexcept;
		RHIBufferHandle CreateBuffer(const RHIBufferDesc& desc,
			const RHIResourceDebugIdentityDesc& debugIdentity = {}) noexcept;
		// Per-description support query against the physical device. Uses
		// the same creation contract as CreateTexture.
		[[nodiscard]] RHITextureSupportResult QueryTextureSupport(
			const RHITextureDesc& desc) const noexcept;
		RHITextureHandle ImportTexture(const ImportedTextureDesc& desc) noexcept;
		RHIBufferHandle ImportBuffer(const ImportedBufferDesc& desc) noexcept;

		void DestroyTexture(RHITextureHandle texture) noexcept;
		void DestroyBuffer(RHIBufferHandle buffer) noexcept;
		void SetTextureDebugBinding(
			RHITextureHandle texture, const RHIResourceDebugBindingDesc& binding) noexcept;
		void SetBufferDebugBinding(
			RHIBufferHandle buffer, const RHIResourceDebugBindingDesc& binding) noexcept;
		std::string_view GetTextureDebugName(RHITextureHandle texture) const noexcept;
		std::string_view GetBufferDebugName(RHIBufferHandle buffer) const noexcept;
		void RecordTextureUse(RHITextureHandle texture, const RHIFencePoint& fencePoint) noexcept;
		void RecordBufferUse(RHIBufferHandle buffer, const RHIFencePoint& fencePoint) noexcept;

		bool IsAlive(RHITextureHandle texture) const noexcept;
		bool IsAlive(RHIBufferHandle buffer) const noexcept;

		void* MapBuffer(RHIBufferHandle buffer, RHIMappedBufferRange readRange) noexcept;
		void UnmapBuffer(RHIBufferHandle buffer, RHIMappedBufferRange writtenRange) noexcept;

		VulkanTexture* ResolveTexture(RHITextureHandle texture) noexcept;
		const VulkanTexture* ResolveTexture(RHITextureHandle texture) const noexcept;
		VulkanBuffer* ResolveBuffer(RHIBufferHandle buffer) noexcept;
		const VulkanBuffer* ResolveBuffer(RHIBufferHandle buffer) const noexcept;
		const RHITextureDesc* ResolveTextureDesc(RHITextureHandle texture) const noexcept;
		const RHIBufferDesc* ResolveBufferDesc(RHIBufferHandle buffer) const noexcept;
		[[nodiscard]] bool ResolveTextureViewBinding(
			RHITextureViewHandle view, TextureViewBinding& outBinding) const noexcept;

		RHITextureViewHandle CreateTextureView(
			RHITextureHandle texture, const RHITextureViewDesc& desc) noexcept;
		RHIBufferViewHandle CreateBufferView(
			RHIBufferHandle buffer, const RHIBufferViewDesc& desc) noexcept;
		RHISamplerHandle CreateSampler(const RHISamplerDesc& desc) noexcept;
		void DestroyTextureView(RHITextureViewHandle view) noexcept;
		void DestroyBufferView(RHIBufferViewHandle view) noexcept;
		void DestroySampler(RHISamplerHandle sampler) noexcept;
		bool IsSamplerAlive(RHISamplerHandle sampler) const noexcept;

		RHIDescriptorHandle GetTextureViewDescriptor(RHITextureViewHandle view) const noexcept;
		RHIDescriptorHandle GetBufferViewDescriptor(RHIBufferViewHandle view) const noexcept;
		RHIDescriptorHandle GetSamplerDescriptor(RHISamplerHandle sampler) const noexcept;
		bool PublishTextureViewDescriptor(RHITextureViewHandle view) noexcept;
		bool PublishSamplerDescriptor(RHISamplerHandle sampler) noexcept;

		void RetireCompletedResources() noexcept;

	private:
		[[nodiscard]] bool CheckOwnerThread(std::string_view operation) const noexcept;
		RHITextureHandle AllocateTextureSlot(std::unique_ptr<VulkanTexture> texture,
			RHIResourceOwnership ownership,
			const RHIResourceDebugIdentityDesc& debugIdentity) noexcept;
		RHIBufferHandle AllocateBufferSlot(std::unique_ptr<VulkanBuffer> buffer,
			RHIResourceOwnership ownership,
			const RHIResourceDebugIdentityDesc& debugIdentity) noexcept;

		[[nodiscard]] VkImageView CreateNativeImageView(const VulkanNormalizedTextureView& normalized,
			VulkanTexture& nativeTexture, std::string_view debugName) noexcept;
		[[nodiscard]] VkSampler CreateNativeSampler(const RHISamplerDesc& desc) noexcept;

		static void RecordLastUsePoint(
			std::vector<RHIFencePoint>& points, const RHIFencePoint& fencePoint) noexcept;
		template <typename HandleT, typename SlotT>
		void SetResourceDebugBinding(RHIHandleTable<HandleT, SlotT>& table, HandleT handle,
			RHIResourceType resourceType, const RHIResourceDebugBindingDesc& binding,
			const char* functionName) noexcept;
		template <typename HandleT, typename SlotT, typename ResourceT>
		static HandleT AllocateResourceSlot(RHIHandleTable<HandleT, SlotT>& table,
			std::unique_ptr<ResourceT> resource, RHIResourceOwnership ownership,
			RHIResourceType resourceType,
			const RHIResourceDebugIdentityDesc& debugIdentity) noexcept;
		template <typename HandleT, typename SlotT>
		static std::string_view GetResourceDebugName(
			const RHIHandleTable<HandleT, SlotT>& table, HandleT handle) noexcept;
		template <typename HandleT, typename SlotT, typename OnValidT>
		void DestroyResource(RHIHandleTable<HandleT, SlotT>& table, HandleT handle,
			const char* functionName, OnValidT onValid) noexcept;
		// View/sampler destroy path. Texture and buffer callbacks join the
		// parent resource's current last-use gate; samplers remain ungated
		// until descriptor publication adds their last-possible-use gate.
		template <typename HandleT, typename SlotT, typename OnValidT>
		void DestroyViewHandle(RHIHandleTable<HandleT, SlotT>& table, HandleT handle,
			const char* functionName, OnValidT onValid) noexcept;
		template <typename HandleT, typename SlotT>
		void RecordResourceUse(RHIHandleTable<HandleT, SlotT>& table, HandleT handle,
			const RHIFencePoint& fencePoint, const char* functionName,
			const char* resourceKind) noexcept;
		template <typename HandleT, typename SlotT>
		void RetireCompletedResourceTable(
			RHIHandleTable<HandleT, SlotT>& table, uint64_t& retireCount) noexcept;
		void RetireCompletedViewTables() noexcept;
		void ReportLiveResources() const noexcept;

	private:
		VulkanDevice* m_Device = nullptr;

		RHIHandleTable<RHITextureHandle, TextureSlot> m_Textures;
		RHIHandleTable<RHIBufferHandle, BufferSlot> m_Buffers;
		RHIHandleTable<RHITextureViewHandle, TextureViewSlot> m_TextureViews;
		RHIHandleTable<RHIBufferViewHandle, BufferViewSlot> m_BufferViews;
		RHIHandleTable<RHISamplerHandle, SamplerSlot> m_Samplers;

		std::unordered_map<RHITextureViewKey, RHITextureViewHandle, RHITextureViewKeyHash>
			m_TextureViewCache;
		std::unordered_map<RHIBufferViewKey, RHIBufferViewHandle, RHIBufferViewKeyHash>
			m_BufferViewCache;
		std::unordered_map<RHISamplerDesc, RHISamplerHandle, KeyHash<RHISamplerDesc>>
			m_SamplerCache;
		std::unordered_map<RHITextureHandle, std::vector<RHITextureViewHandle>>
			m_TextureResourceViews;
		std::unordered_map<RHIBufferHandle, std::vector<RHIBufferViewHandle>>
			m_BufferResourceViews;

		Diagnostics m_Diagnostics;
	};
}
