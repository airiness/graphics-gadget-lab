#pragma once
#include "DX12Descriptor.h"
#include "Color.h"

namespace gglab
{
	class DX12Device;
	class DX12CommandQueue;
	class DX12CommandList;
	class DX12Texture;
	class DX12SwapChain
	{
	public:
		struct CreateInfo
		{
			DX12Device* m_DX12Device = nullptr;
			DX12CommandQueue* m_DX12CommandQueue = nullptr;
			HWND m_Hwnd = nullptr;

			uint32_t m_Width = 0;
			uint32_t m_Height = 0;

			DXGI_FORMAT m_Format = DXGI_FORMAT_R8G8B8A8_UNORM;
			uint32_t m_BufferCount = 0;

			bool m_AllowTearing = true;
			bool m_Vsync = true;
		};

	public:
		DX12SwapChain() noexcept = default;
		GGLAB_DELETE_COPYABLE_MOVABLE(DX12SwapChain);
		~DX12SwapChain();

		bool Initialize(const CreateInfo& info) noexcept;
		void Finalize() noexcept;
		void OnResize(uint32_t width, uint32_t height) noexcept;

		bool IsValid() const noexcept { return m_DxgiSwapChain != nullptr; }

		uint32_t GetBufferWidth() const noexcept { return m_Width; }
		uint32_t GetBufferHeight() const noexcept { return m_Height; }
		DXGI_FORMAT GetFormat() const noexcept { return m_Format; }

		void SetClearColor(const Color& color) noexcept { m_ClearColor = color; }
		const Color& GetClearColor() const noexcept { return m_ClearColor; }

		void WaitFrameCompletion() noexcept;
		void UpdateFrameSyncObject(DX12FencePoint&& fencePoint) noexcept;
		void Present() noexcept;

		uint32_t GetCurrentBackBufferIndex() const noexcept { return m_BackBufferIndex; }
		DX12DescriptorView GetCurrentBackBufferView() const noexcept;
		DX12Texture* GetCurrentBackBuffer() const noexcept;

		void PrepareBackBuffer(DX12CommandList* commandList) const noexcept;
		void FinishBackBuffer(DX12CommandList* commandList) const noexcept;
		void ClearBackBuffer(DX12CommandList* commandList) const noexcept;

	private:
		ComPtr<IDXGISwapChain4> CreateSwapChain() noexcept;
		void CreateRTVs() noexcept;
		void CreateSyncObjects() noexcept;

	private:
		DX12Device* m_DX12Device = nullptr;
		DX12CommandQueue* m_PresentQueue = nullptr;

		HWND m_Hwnd = nullptr;
		uint32_t m_Width = 0;
		uint32_t m_Height = 0;

		DXGI_FORMAT m_Format = DXGI_FORMAT_UNKNOWN;

		uint32_t m_BufferCount = 2;
		bool m_AllowTearing = false;

		std::vector<std::unique_ptr<DX12Texture>> m_BackBuffers;
		std::vector<DX12FencePoint> m_SyncObjects;

		ComPtr<IDXGISwapChain4> m_DxgiSwapChain;

		Color m_ClearColor = color::Gray;

		uint32_t m_BackBufferIndex = 0;
	};
}