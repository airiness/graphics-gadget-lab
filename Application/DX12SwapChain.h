#pragma once
#include "DX12Descriptor.h"

namespace gglab
{
	class DX12Device;
	class DX12CommandQueue;
	class DX12CommandList;
	class DX12Texture;
	class DX12SwapChain
	{
	public:
		explicit DX12SwapChain(DX12Device* dx12Device,
			DX12CommandQueue* dx12CommandQueue,
			uint32_t width, 
			uint32_t height,
			DXGI_FORMAT bufferFormat = DXGI_FORMAT_R8G8B8A8_UNORM) noexcept;
		~DX12SwapChain() noexcept;

		void OnResize(uint32_t width, uint32_t height) noexcept;

		uint32_t GetBufferWidth() const noexcept { return m_Width; }
		uint32_t GetBufferHeight() const noexcept { return m_Height; }
		DXGI_FORMAT GetFormat() const noexcept { return m_Format; }

		void SetClearColor(float r, float g, float b, float a) noexcept;

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
		DX12CommandQueue* m_DX12CommandQueue = nullptr;

		uint32_t m_Width = 0;
		uint32_t m_Height = 0;

		DXGI_FORMAT m_Format = DXGI_FORMAT_UNKNOWN;

		std::vector<DX12Descriptor> m_BackBufferDescriptors;
		std::vector<std::unique_ptr<DX12Texture>> m_BackBuffers;
		std::vector<DX12FencePoint> m_SyncObjects;

		ComPtr<IDXGISwapChain4> m_DxgiSwapChain;

		float m_ClearColor[4] = { 0.6f, 0.6f, 0.6f, 1.0f };

		uint32_t m_BackBufferIndex = 0;
	};
}



