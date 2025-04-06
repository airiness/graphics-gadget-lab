#pragma once

namespace graphicsGadgetLab
{
	class DX12CommandQueue;
	class DX12SwapChain;
	class DX12CommandList;
	class DX12DescriptorHeap;
	class DX12Device
	{
	public:
		DX12Device() noexcept;
		DX12Device(const DX12Device&) = delete;
		DX12Device& operator=(const DX12Device&) = delete;
		DX12Device(DX12Device&&) = delete;
		DX12Device& operator=(DX12Device&&) = delete;
		~DX12Device() noexcept;

		void Initialize() noexcept;
		void OnResize(uint32_t width, uint32_t height) noexcept;
		void Finalize() noexcept;

		ID3D12Device* Get() const noexcept { return m_D3D12Device.Get(); }
		IDXGIFactory7* GetDXGIFactory() const noexcept { return m_DxgiFactory.Get(); }
		IDXGIAdapter1* GetDXGIAdapter() const noexcept { return m_DxgiAdapter.Get(); }

		DX12CommandQueue* GetDirectCommandQueue() const noexcept { return m_DirectCommandQueue.get(); }
		DX12CommandQueue* GetComputeCommandQueue() const noexcept { return m_ComputeCommandQueue.get(); }
		DX12CommandQueue* GetCopyCommandQueue() const noexcept { return m_CopyCommandQueue.get(); }

		uint32_t GetRTVDescriptorSize() const noexcept { return m_RTVDescriptorSize; }
		uint32_t GetDSVDescriptorSize() const noexcept { return m_DSVDescriptorSize; }
		uint32_t GetSRVDescriptorSize() const noexcept { return m_SRVDescriptorSize; }

		bool SupportRayTracing() const noexcept { return m_RayTracingSupported; }
		bool SupportMeshShader() const noexcept { return m_MeshShaderSupported; }
		bool SupportTearing() const noexcept { return m_TearingSupported; }

		static uint32_t GetBufferCount() noexcept { return BufferCount; }
	private:
		void InitializeDXGIFactory() noexcept;
		void InitializeDXGIAdapter() noexcept;
		void InitializeD3D12Device() noexcept;
		void InitializeCommandQueues() noexcept;
		void InitializeSwapChain() noexcept;
		void InitializeCommandLists() noexcept;
		void InitializeDescriptorHeaps() noexcept;

		void CheckFeatureSupport() noexcept;

	private:
		static constexpr uint32_t BufferCount = 2;

	private:
		ComPtr<IDXGIFactory7> m_DxgiFactory;
		ComPtr<IDXGIAdapter1> m_DxgiAdapter;
		ComPtr<ID3D12Device> m_D3D12Device;

		std::unique_ptr<DX12CommandQueue> m_DirectCommandQueue;
		std::unique_ptr<DX12CommandQueue> m_ComputeCommandQueue;
		std::unique_ptr<DX12CommandQueue> m_CopyCommandQueue;

		std::unique_ptr<DX12SwapChain> m_SwapChain;

		D3D_FEATURE_LEVEL m_FeatureLevel = D3D_FEATURE_LEVEL_12_0;

		uint32_t m_RTVDescriptorSize = 0;
		uint32_t m_SRVDescriptorSize = 0;
		uint32_t m_DSVDescriptorSize = 0;

		// supported features
		bool m_RayTracingSupported = false;
		bool m_MeshShaderSupported = false;
		bool m_TearingSupported = false;

		std::array<std::unique_ptr<DX12CommandList>, BufferCount> m_GraphicsCommandLists;
		std::array<std::unique_ptr<DX12CommandList>, BufferCount> m_ComputeCommandLists;
		std::array<std::unique_ptr<DX12CommandList>, BufferCount> m_CopyCommandLists;

		std::unique_ptr<DX12DescriptorHeap> m_CbvSrvUavDescriptorHeap;
		std::unique_ptr<DX12DescriptorHeap> m_RtvDescriptorHeap;
		std::unique_ptr<DX12DescriptorHeap> m_DsvDescriptorHeap;
		std::unique_ptr<DX12DescriptorHeap> m_SamplerDescriptorHeap;
	};
}