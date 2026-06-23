#pragma once
#include "Core/CoreMacros.h"
#include "Graphics/RHI/RHIBuffer.h"
#include "Graphics/RHI/RHITexture.h"

#include <memory>
#include <vector>

namespace gglab
{
	class DX12Buffer;
	class DX12Device;
	class DX12Texture;

	enum class DX12ResourceOwnership : uint8_t
	{
		Owned,
		Borrowed,
	};

	class DX12ResourceManager
	{
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
			DX12ResourceOwnership m_Ownership = DX12ResourceOwnership::Owned;
			bool m_Alive = false;
			std::unique_ptr<DX12Texture> m_Texture;
		};

		struct BufferSlot
		{
			RHIBufferHandle::GenerationType m_Generation = 1;
			DX12ResourceOwnership m_Ownership = DX12ResourceOwnership::Owned;
			bool m_Alive = false;
			std::unique_ptr<DX12Buffer> m_Buffer;
		};

	private:
		uint32_t AllocateTextureSlot() noexcept;
		uint32_t AllocateBufferSlot() noexcept;

	private:
		DX12Device* m_Device = nullptr;

		std::vector<TextureSlot> m_TextureSlots;
		std::vector<uint32_t> m_FreeTextureSlots;
		std::vector<std::unique_ptr<DX12Texture>> m_PendingDestroyedTextures;

		std::vector<BufferSlot> m_BufferSlots;
		std::vector<uint32_t> m_FreeBufferSlots;
		std::vector<std::unique_ptr<DX12Buffer>> m_PendingDestroyedBuffers;
	};
}
