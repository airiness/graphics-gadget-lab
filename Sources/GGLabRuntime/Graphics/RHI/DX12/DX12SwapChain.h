#pragma once
#include "GGLabFoundation/Base/CoreMacros.h"
#include "GGLabFoundation/Platform/Win/ComTypes.h"
#include "Graphics/RHI/RHISwapChain.h"

#include <cstdint>
#include <vector>

#include <dxgi1_6.h>
#include <windows.h>

namespace gglab
{
	class DX12Device;
	class DX12CommandQueue;
	class DX12Context;
	class DX12Texture;
	class DX12SwapChain final : public RHISwapChain
	{
	public:
		struct CreateInfo
		{
			DX12Device* m_DX12Device = nullptr;
			DX12CommandQueue* m_PresentQueue = nullptr;
			HWND m_Hwnd = nullptr;

			uint32_t m_Width = 0;
			uint32_t m_Height = 0;

			DXGI_FORMAT m_Format = DXGI_FORMAT_R8G8B8A8_UNORM;
			uint32_t m_BufferCount = 0;

			bool m_AllowTearing = false;
			bool m_Vsync = true;
		};

	public:
		DX12SwapChain() noexcept = default;
		GGLAB_DELETE_COPYABLE_DEFAULT_MOVABLE(DX12SwapChain);
		~DX12SwapChain();

		bool Initialize(const CreateInfo& createInfo) noexcept;
		void Finalize() noexcept;

		bool IsValid() const noexcept override { return m_DxgiSwapChain != nullptr; }

		void Resize(uint32_t width, uint32_t height) noexcept;

		uint32_t GetBufferCount() const noexcept override { return m_BufferCount; }
		uint32_t GetBufferWidth() const noexcept override { return m_Width; }
		uint32_t GetBufferHeight() const noexcept override { return m_Height; }
		RHIFormat GetFormat() const noexcept override;

		RHITextureHandle GetBackBufferHandle(uint32_t bufferIndex) const noexcept override;
		DX12Texture* GetBackBuffer(uint32_t bufferIndex) const noexcept;

	private:
		friend class DX12Context;

		[[nodiscard]] uint32_t GetCurrentBackBufferIndex() const noexcept
		{
			return m_BackBufferIndex;
		}
		[[nodiscard]] bool Present() noexcept;
		ComPtr<IDXGISwapChain4> CreateSwapChain() noexcept;
		void AcquireBackBuffers() noexcept;
		void ReleaseBackBuffers() noexcept;
		void RefreshCurrentBackBufferIndex() noexcept;
		void Reset() noexcept;

	private:
		DX12Device* m_DX12Device = nullptr;
		DX12CommandQueue* m_PresentQueue = nullptr;

		HWND m_Hwnd = nullptr;
		uint32_t m_Width = 0;
		uint32_t m_Height = 0;

		DXGI_FORMAT m_Format = DXGI_FORMAT_UNKNOWN;

		uint32_t m_BufferCount = 2;
		uint32_t m_BackBufferIndex = 0;

		bool m_AllowTearing = false;
		bool m_Vsync = true;

		ComPtr<IDXGISwapChain4> m_DxgiSwapChain;

		std::vector<RHITextureHandle> m_BackBuffers;
	};
}
