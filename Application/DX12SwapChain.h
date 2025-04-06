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
			DXGI_FORMAT bufferFormat = DXGI_FORMAT_R8G8B8A8_UNORM) noexcept;
		~DX12SwapChain() noexcept;

		// TODO: Resize SwapChain support
		//void Resize(uint32_t width, uint32_t height) noexcept;

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

		ComPtr<ID3D12DescriptorHeap> m_RTVHeap;
		std::vector<D3D12_CPU_DESCRIPTOR_HANDLE> m_RTVHandles;
		std::vector<ComPtr<ID3D12Resource>> m_BackBuffers;
	};
}



