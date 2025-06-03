#pragma once
#include "DX12FencePoint.h"

namespace D3D12MA
{
	class Allocator;
}

namespace graphicsGadgetLab
{
	class DX12CommandQueue;
	class DX12SwapChain;
	class DX12CommandList;
	class DX12DescriptorHeap;
	class DX12Fence;
	class DX12CommandAllocatorPool;
	class DX12Resource;
	class DX12Device
	{
	public:
		struct DX12FeatureSupport
		{
			bool m_RayTracingSupported = false;
			bool m_MeshShaderSupported = false;
			bool m_TearingSupported = false;
			bool m_EnhancedBarriers = false;
		};

	public:
		DX12Device() noexcept;
		GGLAB_DELETE_COPYABLE_MOVABLE(DX12Device);
		~DX12Device() noexcept;

		void Initialize() noexcept;
		void OnResize(uint32_t width, uint32_t height) noexcept;
		void Finalize() noexcept;

		ID3D12Device10* Get() const noexcept { return m_D3D12Device.Get(); }
		IDXGIFactory7* GetDXGIFactory() const noexcept { return m_DxgiFactory.Get(); }
		IDXGIAdapter1* GetDXGIAdapter() const noexcept { return m_DxgiAdapter.Get(); }
		
		D3D12MA::Allocator* GetMemAllocator() const noexcept { return m_MemAllocator.Get(); }

		DX12SwapChain* GetSwapChain() const noexcept { return m_SwapChain.get(); }

		DX12CommandQueue* GetGraphicsCommandQueue() const noexcept { return m_DirectCommandQueue.get(); }
		DX12CommandQueue* GetComputeCommandQueue() const noexcept { return m_ComputeCommandQueue.get(); }
		DX12CommandQueue* GetCopyCommandQueue() const noexcept { return m_CopyCommandQueue.get(); }

		DX12CommandList* GetGraphicsCommandList(uint32_t bufferIndex) const noexcept { return m_GraphicsCommandLists.at(bufferIndex).get(); }
		DX12CommandList* GetComputeCommandList(uint32_t bufferIndex) const noexcept { return m_ComputeCommandLists.at(bufferIndex).get(); }
		DX12CommandList* GetCopyCommandList(uint32_t bufferIndex) const noexcept { return m_CopyCommandLists.at(bufferIndex).get(); }

		DX12CommandAllocatorPool* GetGraphicsCommandAllocatorPool() const noexcept { return m_GraphicsCommandAllocatorPool.get(); }
		DX12CommandAllocatorPool* GetComputeCommandAllocatorPool() const noexcept { return m_ComputeCommandAllocatorPool.get(); }
		DX12CommandAllocatorPool* GetCopyCommandAllocatorPool() const noexcept { return m_CopyCommandAllocatorPool.get(); }

		uint32_t GetRtvDescriptorSize() const noexcept { return m_RtvDescriptorSize; }
		uint32_t GetDsvDescriptorSize() const noexcept { return m_DsvDescriptorSize; }
		uint32_t GetCbvSrvUavDescriptorSize() const noexcept { return m_CbvSrvUavDescriptorSize; }

		DX12DescriptorHeap* GetRtvDescriptorHeap() const noexcept { return m_RtvDescriptorHeap.get(); }
		DX12DescriptorHeap* GetDsvDescriptorHeap() const noexcept { return m_DsvDescriptorHeap.get(); }
		DX12DescriptorHeap* GetCbvSrvUavDescriptorHeap() const noexcept { return m_CbvSrvUavDescriptorHeap.get(); }
		DX12DescriptorHeap* GetSamplerDescriptorHeap() const noexcept { return m_SamplerDescriptorHeap.get(); }

		bool SupportRayTracing() const noexcept { return m_DX12FeatureSupport.m_RayTracingSupported; }
		bool SupportMeshShader() const noexcept { return m_DX12FeatureSupport.m_MeshShaderSupported; }
		bool SupportTearing() const noexcept { return m_DX12FeatureSupport.m_TearingSupported; }

		void BeginUpload() noexcept;
		void EndUpload(bool wait) noexcept;

		void UploadResource(const void* data, size_t dataSize, const DX12Resource* destResource) noexcept;

	public:
		static uint32_t GetBufferCount() noexcept { return BufferCount; }

	private:
		void InitializeDXGIFactory() noexcept;
		void InitializeDXGIAdapter() noexcept;
		void InitializeD3D12Device() noexcept;
		void InitializeCommandQueues() noexcept;
		void InitializeSwapChain() noexcept;
		void InitializeCommandLists() noexcept;
		void InitializeCommandAllocatorPools() noexcept;
		void InitializeDescriptorHeaps() noexcept;
		void InitializeMemAllocator() noexcept;
		void FinalizeMemAllocator() noexcept;

		void CheckFeatureSupport() noexcept;

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
		std::unique_ptr<DX12CommandQueue> m_UploadCommmandQueue;

		std::unique_ptr<DX12SwapChain> m_SwapChain;

		D3D_FEATURE_LEVEL m_FeatureLevel = D3D_FEATURE_LEVEL_12_0;

		uint32_t m_RtvDescriptorSize = 0;
		uint32_t m_DsvDescriptorSize = 0;
		uint32_t m_CbvSrvUavDescriptorSize = 0;

		// supported features
		DX12FeatureSupport m_DX12FeatureSupport;

		std::array<std::unique_ptr<DX12CommandList>, BufferCount> m_GraphicsCommandLists;
		std::array<std::unique_ptr<DX12CommandList>, BufferCount> m_ComputeCommandLists;
		std::array<std::unique_ptr<DX12CommandList>, BufferCount> m_CopyCommandLists;
		std::unique_ptr<DX12CommandList> m_UploadCommandList;

		// Command Allocator Pool
		std::unique_ptr<DX12CommandAllocatorPool> m_GraphicsCommandAllocatorPool;
		std::unique_ptr<DX12CommandAllocatorPool> m_ComputeCommandAllocatorPool;
		std::unique_ptr<DX12CommandAllocatorPool> m_CopyCommandAllocatorPool;
		std::unique_ptr<DX12CommandAllocatorPool> m_UploadCommandAllocatorPool;
		
		std::unique_ptr<DX12DescriptorHeap> m_CbvSrvUavDescriptorHeap;
		std::unique_ptr<DX12DescriptorHeap> m_RtvDescriptorHeap;
		std::unique_ptr<DX12DescriptorHeap> m_DsvDescriptorHeap;
		std::unique_ptr<DX12DescriptorHeap> m_SamplerDescriptorHeap;
		// TODO: Descriptor Allocator!

		DX12FencePoint m_UploadFencePoint;
		std::vector<std::unique_ptr<DX12Resource>> m_UploadIntermediateResources;
	};
}