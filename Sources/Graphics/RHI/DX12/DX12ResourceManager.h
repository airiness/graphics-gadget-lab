#pragma once
#include "Core/CoreMacros.h"
#include "Graphics/RHI/RHIBuffer.h"
#include "Graphics/RHI/RHIFence.h"
#include "Graphics/RHI/RHIHandleTable.h"
#include "Graphics/RHI/RHIResourceDebug.h"
#include "Graphics/RHI/RHITexture.h"
#include "Core/Platform/Win/ComTypes.h"

#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace gglab
{
	class DX12Buffer;
	class DX12Device;
	class DX12ResourceManager;
	class DX12Texture;
	class DX12DescriptorCache;
	struct DX12ResourceManagerSnapshot;
	void BuildDX12ResourceManagerSnapshot(
		const DX12ResourceManager& manager, DX12ResourceManagerSnapshot& outSnapshot) noexcept;

	class DX12ResourceManager
	{
	public:
		struct ImportedTextureDesc
		{
			RHIImportedTextureDesc m_RHI;
			ComPtr<ID3D12Resource> m_Resource;
			RHIResourceDebugIdentityDesc m_DebugIdentity;
		};

		struct ImportedBufferDesc
		{
			RHIImportedBufferDesc m_RHI;
			ComPtr<ID3D12Resource> m_Resource;
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
			uint64_t m_UnnamedResourceCreateCount = 0;
		};

	public:
		DX12ResourceManager() noexcept = default;
		GGLAB_DELETE_COPYABLE_MOVABLE(DX12ResourceManager);
		~DX12ResourceManager() noexcept;

		void Initialize(DX12Device* device) noexcept;
		void Finalize() noexcept;
		void SetDescriptorCache(DX12DescriptorCache* descriptorCache) noexcept
		{
			m_DescriptorCache = descriptorCache;
		}

		RHITextureHandle CreateTexture(const RHITextureDesc& desc,
			const RHIResourceDebugIdentityDesc& debugIdentity = {}) noexcept;
		RHIBufferHandle CreateBuffer(const RHIBufferDesc& desc,
			const RHIResourceDebugIdentityDesc& debugIdentity = {}) noexcept;
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

		DX12Texture* ResolveTexture(RHITextureHandle texture) noexcept;
		const DX12Texture* ResolveTexture(RHITextureHandle texture) const noexcept;
		DX12Buffer* ResolveBuffer(RHIBufferHandle buffer) noexcept;
		const DX12Buffer* ResolveBuffer(RHIBufferHandle buffer) const noexcept;

		void RetireCompletedResources() noexcept;

	private:
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
			std::unique_ptr<ResourceT> m_Resource;
		};

		using TextureSlot = ResourceSlot<RHITextureHandle, DX12Texture>;
		using BufferSlot = ResourceSlot<RHIBufferHandle, DX12Buffer>;

	private:
		RHITextureHandle AllocateTextureSlot(std::unique_ptr<DX12Texture> texture,
			RHIResourceOwnership ownership,
			const RHIResourceDebugIdentityDesc& debugIdentity) noexcept;
		RHIBufferHandle AllocateBufferSlot(std::unique_ptr<DX12Buffer> buffer,
			RHIResourceOwnership ownership,
			const RHIResourceDebugIdentityDesc& debugIdentity) noexcept;
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
		template <typename HandleT, typename SlotT>
		void RecordResourceUse(RHIHandleTable<HandleT, SlotT>& table, HandleT handle,
			const RHIFencePoint& fencePoint, const char* functionName,
			const char* resourceKind) noexcept;
		template <typename HandleT, typename SlotT>
		void RetireCompletedResourceTable(
			RHIHandleTable<HandleT, SlotT>& table, uint64_t& retireCount) noexcept;
		void ReportLiveResources() const noexcept;

	private:
		DX12Device* m_Device = nullptr;
		DX12DescriptorCache* m_DescriptorCache = nullptr;

		RHIHandleTable<RHITextureHandle, TextureSlot> m_Textures;
		RHIHandleTable<RHIBufferHandle, BufferSlot> m_Buffers;

		Diagnostics m_Diagnostics;

		friend void BuildDX12ResourceManagerSnapshot(
			const DX12ResourceManager& manager, DX12ResourceManagerSnapshot& outSnapshot) noexcept;
	};
}
