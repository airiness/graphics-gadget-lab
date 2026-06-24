#pragma once
#include "Core/CoreMacros.h"
#include "Core/StringId.h"
#include "Graphics/RHI/RHIBuffer.h"
#include "Graphics/RHI/RHIHandleTable.h"
#include "Graphics/RHI/RHITexture.h"
#include "Graphics/RHI/DX12/DX12FencePoint.h"
#include "Core/Platform/Win/ComTypes.h"

#include <memory>
#include <string_view>
#include <vector>

namespace gglab
{
	class DX12Buffer;
	class DX12Device;
	class DX12ResourceManager;
	class DX12Texture;
	class DX12ViewCache;
	struct DX12ResourceManagerSnapshot;
	void BuildDX12ResourceManagerSnapshot(
		const DX12ResourceManager& manager,
		DX12ResourceManagerSnapshot& outSnapshot) noexcept;

	class DX12ResourceManager
	{
	public:
		struct ImportedTextureDesc
		{
			RHIImportedTextureDesc m_RHI;
			ComPtr<ID3D12Resource> m_Resource;
		};

		struct ImportedBufferDesc
		{
			RHIImportedBufferDesc m_RHI;
			ComPtr<ID3D12Resource> m_Resource;
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

	public:
		DX12ResourceManager() noexcept = default;
		GGLAB_DELETE_COPYABLE_MOVABLE(DX12ResourceManager);
		~DX12ResourceManager() noexcept;

		void Initialize(DX12Device* device) noexcept;
		void Finalize() noexcept;
		void SetViewCache(DX12ViewCache* viewCache) noexcept { m_ViewCache = viewCache; }

		RHITextureHandle CreateTexture(const RHITextureDesc& desc) noexcept;
		RHIBufferHandle CreateBuffer(const RHIBufferDesc& desc) noexcept;
		RHITextureHandle ImportTexture(const ImportedTextureDesc& desc) noexcept;
		RHIBufferHandle ImportBuffer(const ImportedBufferDesc& desc) noexcept;

		void DestroyTexture(RHITextureHandle texture) noexcept;
		void DestroyBuffer(RHIBufferHandle buffer) noexcept;
		void RecordTextureUse(RHITextureHandle texture, const DX12FencePoint& fencePoint) noexcept;
		void RecordBufferUse(RHIBufferHandle buffer, const DX12FencePoint& fencePoint) noexcept;

		bool IsAlive(RHITextureHandle texture) const noexcept;
		bool IsAlive(RHIBufferHandle buffer) const noexcept;

		DX12Texture* ResolveTexture(RHITextureHandle texture) noexcept;
		const DX12Texture* ResolveTexture(RHITextureHandle texture) const noexcept;
		DX12Buffer* ResolveBuffer(RHIBufferHandle buffer) noexcept;
		const DX12Buffer* ResolveBuffer(RHIBufferHandle buffer) const noexcept;

		void RetireCompletedResources() noexcept;

	private:
		struct TextureSlot
		{
			RHITextureHandle::GenerationType m_Generation = 1;
			RHIResourceOwnership m_Ownership = RHIResourceOwnership::Owned;
			RHIHandleSlotState m_State = RHIHandleSlotState::Free;
			StringID m_DebugNameId;
			std::vector<DX12FencePoint> m_LastUsePoints;
			std::vector<DX12FencePoint> m_RetirementPoints;
			std::unique_ptr<DX12Texture> m_Texture;
		};

		struct BufferSlot
		{
			RHIBufferHandle::GenerationType m_Generation = 1;
			RHIResourceOwnership m_Ownership = RHIResourceOwnership::Owned;
			RHIHandleSlotState m_State = RHIHandleSlotState::Free;
			StringID m_DebugNameId;
			std::vector<DX12FencePoint> m_LastUsePoints;
			std::vector<DX12FencePoint> m_RetirementPoints;
			std::unique_ptr<DX12Buffer> m_Buffer;
		};

	private:
		RHITextureHandle AllocateTextureSlot(
			std::unique_ptr<DX12Texture> texture,
			RHIResourceOwnership ownership,
			std::string_view debugName) noexcept;
		RHIBufferHandle AllocateBufferSlot(
			std::unique_ptr<DX12Buffer> buffer,
			RHIResourceOwnership ownership,
			std::string_view debugName) noexcept;
		static void RecordLastUsePoint(
			std::vector<DX12FencePoint>& points,
			const DX12FencePoint& fencePoint) noexcept;
		void ReportLiveResources() const noexcept;

	private:
		DX12Device* m_Device = nullptr;
		DX12ViewCache* m_ViewCache = nullptr;

		RHIHandleTable<RHITextureHandle, TextureSlot> m_Textures;
		RHIHandleTable<RHIBufferHandle, BufferSlot> m_Buffers;

		Diagnostics m_Diagnostics;

		friend void BuildDX12ResourceManagerSnapshot(
			const DX12ResourceManager& manager,
			DX12ResourceManagerSnapshot& outSnapshot) noexcept;
	};
}
