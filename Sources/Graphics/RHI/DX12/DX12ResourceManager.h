#pragma once
#include "Core/CoreMacros.h"
#include "Core/StringId.h"
#include "Graphics/RHI/RHIBuffer.h"
#include "Graphics/RHI/RHITexture.h"
#include "Graphics/RHI/DX12/DX12FencePoint.h"

#include <memory>
#include <vector>

namespace gglab
{
	class DX12Buffer;
	class DX12Device;
	class DX12ResourceManager;
	class DX12Texture;
	struct DX12ResourceManagerSnapshot;
	void BuildDX12ResourceManagerSnapshot(
		const DX12ResourceManager& manager,
		DX12ResourceManagerSnapshot& outSnapshot) noexcept;

	class DX12ResourceManager
	{
	private:
		enum class ResourceOwnership : uint8_t
		{
			Owned,
			Borrowed,
		};

		enum class ResourceSlotState : uint8_t
		{
			Free,
			Alive,
			PendingRetirement,
		};

		struct Diagnostics
		{
			uint64_t m_TextureCreateCount = 0;
			uint64_t m_BufferCreateCount = 0;
			uint64_t m_TextureRetireCount = 0;
			uint64_t m_BufferRetireCount = 0;
			uint64_t m_CreateFailureCount = 0;
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

		RHITextureHandle CreateTexture(const RHITextureDesc& desc) noexcept;
		RHIBufferHandle CreateBuffer(const RHIBufferDesc& desc) noexcept;

		void DestroyTexture(RHITextureHandle texture) noexcept;
		void DestroyBuffer(RHIBufferHandle buffer) noexcept;

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
			ResourceOwnership m_Ownership = ResourceOwnership::Owned;
			ResourceSlotState m_State = ResourceSlotState::Free;
			StringID m_DebugNameId;
			std::vector<DX12FencePoint> m_RetirementPoints;
			std::unique_ptr<DX12Texture> m_Texture;
		};

		struct BufferSlot
		{
			RHIBufferHandle::GenerationType m_Generation = 1;
			ResourceOwnership m_Ownership = ResourceOwnership::Owned;
			ResourceSlotState m_State = ResourceSlotState::Free;
			StringID m_DebugNameId;
			std::vector<DX12FencePoint> m_RetirementPoints;
			std::unique_ptr<DX12Buffer> m_Buffer;
		};

	private:
		uint32_t AllocateTextureSlot() noexcept;
		uint32_t AllocateBufferSlot() noexcept;
		std::vector<DX12FencePoint> CaptureRetirementPoints() const noexcept;
		void ReportLiveResources() const noexcept;

	private:
		DX12Device* m_Device = nullptr;

		std::vector<TextureSlot> m_TextureSlots;
		std::vector<uint32_t> m_FreeTextureSlots;

		std::vector<BufferSlot> m_BufferSlots;
		std::vector<uint32_t> m_FreeBufferSlots;

		Diagnostics m_Diagnostics;

		friend void BuildDX12ResourceManagerSnapshot(
			const DX12ResourceManager& manager,
			DX12ResourceManagerSnapshot& outSnapshot) noexcept;
	};
}
