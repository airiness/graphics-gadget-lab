#pragma once
namespace graphicsGadgetLab
{
	class DX12Device;
	class DX12CommandQueue;
	class DX12CommandList;
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

		void Present() noexcept;

		uint32_t GetCurrentBackBufferIndex() const noexcept { return m_BackBufferIndex; }

		ID3D12Resource* GetCurrentBackBuffer() const noexcept;

		void PrepareBackBuffer(DX12CommandList* commandList) const noexcept;
		void FinishBackBuffer(DX12CommandList* commandList) noexcept;
	private:
		ComPtr<IDXGISwapChain4> CreateSwapChain() noexcept;
		void CreateRTVs() noexcept;

	private:
		DX12Device* m_DX12Device = nullptr;
		DX12CommandQueue* m_DX12CommandQueue = nullptr;

		uint32_t m_Width = 0;
		uint32_t m_Height = 0;

		DXGI_FORMAT m_Format = DXGI_FORMAT_UNKNOWN;

		ComPtr<IDXGISwapChain4> m_DxgiSwapChain;

		//ComPtr<ID3D12DescriptorHeap> m_RtvHeap; // TODO: Get Descriptor From Device; Make Descriptor Allocator
		std::vector<DX12Descriptor> m_BackBufferDescriptors;
		std::vector<ComPtr<ID3D12Resource>> m_BackBuffers;

		uint32_t m_BackBufferIndex = 0;
	};
}



