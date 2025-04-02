#pragma once
namespace graphicsGadgetLab
{
	class DX12Device;
	class DX12CommandQueue;
	class DX12SwapChain
	{
	public:
		explicit DX12SwapChain(DX12Device* dx12Device,
			DX12CommandQueue* dx12CommandQueue,
			uint32_t width, 
			uint32_t height,
			DXGI_FORMAT bufferFormat) noexcept;
		~DX12SwapChain() noexcept;

	private:
		ComPtr<IDXGISwapChain4> CreateSwapChain() noexcept;

	private:
		DX12Device* m_DX12Device = nullptr;
		DX12CommandQueue* m_DX12CommandQueue = nullptr;

		uint32_t m_Width = 0;
		uint32_t m_Height = 0;

		DXGI_FORMAT m_BufferFormat;

		std::vector<ComPtr<ID3D12Resource>> m_SwapChainBuffers;

		ComPtr<IDXGISwapChain4> m_DxgiSwapChain;
	};
}



