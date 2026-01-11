#pragma once

namespace D3D12MA
{
	class Allocator;
}

namespace gglab
{
	class DX12CommandQueue;
	class DX12CommandList;
	class DX12DescriptorFreeListAllocator;
	class DX12CommandAllocatorPool;
	class DX12Resource;
	class DX12Device
	{
	public:
		struct FeatureSupport
		{
			bool m_RayTracingSupported = false;
			bool m_MeshShaderSupported = false;
			bool m_TearingSupported = false;
			bool m_EnhancedBarriers = false;
		};

		struct CreateInfo
		{
			bool m_TryLoadWinPix = true;
		};

	public:
		DX12Device() noexcept = default;
		GGLAB_DELETE_COPYABLE_MOVABLE(DX12Device);
		~DX12Device();

		void Initialize(const CreateInfo& createInfo = {}) noexcept;
		void Finalize() noexcept;

		ID3D12Device10* Get() const noexcept { return m_D3D12Device.Get(); }
		IDXGIFactory7* GetDXGIFactory() const noexcept { return m_DxgiFactory.Get(); }
		IDXGIAdapter1* GetDXGIAdapter() const noexcept { return m_DxgiAdapter.Get(); }

		D3D12MA::Allocator* GetMemAllocator() const noexcept { return m_MemAllocator.Get(); }

		DX12CommandQueue* GetGraphicsCommandQueue() const noexcept { return m_DirectCommandQueue.get(); }
		DX12CommandQueue* GetComputeCommandQueue() const noexcept { return m_ComputeCommandQueue.get(); }
		DX12CommandQueue* GetCopyCommandQueue() const noexcept { return m_CopyCommandQueue.get(); }
		DX12CommandQueue* GetTransferCommandQueue() const noexcept { return m_TransferCommandQueue.get(); }

		DX12CommandList* GetGraphicsCommandList(uint32_t bufferIndex) const noexcept { return m_GraphicsCommandLists.at(bufferIndex).get(); }
		DX12CommandList* GetComputeCommandList(uint32_t bufferIndex) const noexcept { return m_ComputeCommandLists.at(bufferIndex).get(); }

		DX12CommandAllocatorPool* GetGraphicsCommandAllocatorPool() const noexcept { return m_GraphicsCommandAllocatorPool.get(); }
		DX12CommandAllocatorPool* GetComputeCommandAllocatorPool() const noexcept { return m_ComputeCommandAllocatorPool.get(); }
		DX12CommandAllocatorPool* GetCopyCommandAllocatorPool() const noexcept { return m_CopyCommandAllocatorPool.get(); }
		DX12CommandAllocatorPool* GetTransferCommandAllocatorPool() const noexcept { return m_TransferCommandAllocatorPool.get(); }

		bool SupportRayTracing() const noexcept { return m_FeatureSupport.m_RayTracingSupported; }
		bool SupportMeshShader() const noexcept { return m_FeatureSupport.m_MeshShaderSupported; }
		bool SupportTearing() const noexcept { return m_FeatureSupport.m_TearingSupported; }
		bool SupportEnhancedBarrier() const noexcept { return m_FeatureSupport.m_EnhancedBarriers; }

		void FlushGPU() noexcept;

	public:
		static uint32_t GetBufferCount() noexcept { return BufferCount; }

	private:
		void InitializeWinPIX() noexcept;
		void InitializeDXGIFactory() noexcept;
		void InitializeDXGIAdapter() noexcept;
		void InitializeDebugLayer() noexcept;
		void InitializeD3D12Device() noexcept;
		void InitializeInfoQueue() noexcept;
		void InitializeCommandQueues() noexcept;
		void InitializeCommandLists() noexcept;
		void InitializeCommandAllocatorPools() noexcept;
		void InitializeMemAllocator() noexcept;
		void CheckFeatureSupport() noexcept;

	private:
		static std::wstring GetLatestWinPixGpuCapturerPath() noexcept;

	private:
		static constexpr uint32_t BufferCount = 2;

	private:
		ComPtr<IDXGIFactory7> m_DxgiFactory;
		ComPtr<IDXGIAdapter1> m_DxgiAdapter;
		ComPtr<ID3D12Device10> m_D3D12Device;

		ComPtr<D3D12MA::Allocator> m_MemAllocator;

		std::unique_ptr<DX12CommandQueue> m_DirectCommandQueue;
		std::unique_ptr<DX12CommandQueue> m_ComputeCommandQueue;
		std::unique_ptr<DX12CommandQueue> m_CopyCommandQueue;
		std::unique_ptr<DX12CommandQueue> m_TransferCommandQueue;

		D3D_FEATURE_LEVEL m_FeatureLevel = D3D_FEATURE_LEVEL_12_0;

		// supported features
		FeatureSupport m_FeatureSupport;

		std::array<std::unique_ptr<DX12CommandList>, BufferCount> m_GraphicsCommandLists;
		std::array<std::unique_ptr<DX12CommandList>, BufferCount> m_ComputeCommandLists;

		// Command Allocator Pool
		std::unique_ptr<DX12CommandAllocatorPool> m_GraphicsCommandAllocatorPool;
		std::unique_ptr<DX12CommandAllocatorPool> m_ComputeCommandAllocatorPool;
		std::unique_ptr<DX12CommandAllocatorPool> m_CopyCommandAllocatorPool;
		std::unique_ptr<DX12CommandAllocatorPool> m_TransferCommandAllocatorPool;

		bool m_IsInitialized = false;
	};
}