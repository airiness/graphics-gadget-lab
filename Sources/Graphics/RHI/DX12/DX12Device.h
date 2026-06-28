#pragma once
#include "Core/Platform/Win/ComTypes.h"
#include "Core/Utility/TypeUtils.h"
#include "Graphics/RHI/RHIDevice.h"
#include "Graphics/RHI/RHIFence.h"
#include "Graphics/RHI/DX12/DX12ResourceManager.h"

#include <memory>

namespace D3D12MA
{
	class Allocator;
}

namespace gglab
{
	class DX12QueueSystem;
	class DX12DescriptorManager;
	class DX12DescriptorFreeListAllocator;
	class DX12FencePoint;
	class DX12Resource;
	class DX12DescriptorCache;
	class DX12PipelineState;
	class DX12RootSignature;
	struct DX12DescriptorView;
	class DX12Device : public RHIDevice
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
		DX12Device() noexcept;
		GGLAB_DELETE_COPYABLE_MOVABLE(DX12Device);
		~DX12Device();

		void Initialize(const CreateInfo& createInfo = {}) noexcept;
		void Finalize() noexcept;

		ID3D12Device10* Get() const noexcept { return m_D3D12Device.Get(); }
		IDXGIFactory7* GetDXGIFactory() const noexcept { return m_DxgiFactory.Get(); }
		IDXGIAdapter1* GetDXGIAdapter() const noexcept { return m_DxgiAdapter.Get(); }

		D3D12MA::Allocator* GetMemAllocator() const noexcept { return m_MemAllocator.Get(); }

		bool SupportRayTracing() const noexcept { return m_FeatureSupport.m_RayTracingSupported; }
		bool SupportMeshShader() const noexcept { return m_FeatureSupport.m_MeshShaderSupported; }
		bool SupportTearing() const noexcept { return m_FeatureSupport.m_TearingSupported; }
		bool SupportEnhancedBarrier() const noexcept { return m_FeatureSupport.m_EnhancedBarriers; }

		RHIBackendType GetBackendType() const noexcept override { return RHIBackendType::DX12; }

		RHITextureHandle CreateTexture(const RHITextureDesc& desc) noexcept override;
		RHIBufferHandle CreateBuffer(const RHIBufferDesc& desc) noexcept override;
		RHITextureHandle ImportTexture(const DX12ResourceManager::ImportedTextureDesc& desc) noexcept;
		RHIBufferHandle ImportBuffer(const DX12ResourceManager::ImportedBufferDesc& desc) noexcept;
		RHITextureViewHandle CreateTextureView(RHITextureHandle texture, const RHITextureViewDesc& desc) noexcept override;
		RHIBufferViewHandle CreateBufferView(RHIBufferHandle buffer, const RHIBufferViewDesc& desc) noexcept override;
		RHISamplerHandle CreateSampler(const RHISamplerDesc& desc) noexcept override;

		void DestroyTexture(RHITextureHandle texture) noexcept override;
		void DestroyBuffer(RHIBufferHandle buffer) noexcept override;
		void DestroyTextureView(RHITextureViewHandle view) noexcept override;
		void DestroyBufferView(RHIBufferViewHandle view) noexcept override;
		void DestroySampler(RHISamplerHandle sampler) noexcept override;
		void* MapBuffer(RHIBufferHandle buffer) noexcept override;
		void UnmapBuffer(RHIBufferHandle buffer) noexcept override;
		uint32_t GetBufferViewAlignment(RHIBufferViewType viewType) const noexcept override;
		bool IsFencePointCompleted(const RHIFencePoint& fencePoint) const noexcept override;
		void RecordTextureUse(RHITextureHandle texture, const DX12FencePoint& fencePoint) noexcept;
		void RecordTextureUse(RHITextureHandle texture, const RHIFencePoint& fencePoint) noexcept override;
		void RecordBufferUse(RHIBufferHandle buffer, const DX12FencePoint& fencePoint) noexcept;
		void RecordBufferUse(RHIBufferHandle buffer, const RHIFencePoint& fencePoint) noexcept override;
		void SetDescriptorManager(DX12DescriptorManager* descriptorManager) noexcept;
		void SetQueueSystem(DX12QueueSystem* queueSystem) noexcept { m_QueueSystem = queueSystem; }

		bool IsAlive(RHITextureHandle texture) const noexcept override;
		bool IsAlive(RHIBufferHandle buffer) const noexcept override;
		bool IsAlive(RHISamplerHandle sampler) const noexcept override;
		RHIDescriptorHandle GetTextureViewDescriptor(RHITextureViewHandle view) const noexcept override;
		RHIDescriptorHandle GetBufferViewDescriptor(RHIBufferViewHandle view) const noexcept override;
		RHIDescriptorHandle GetSamplerDescriptor(RHISamplerHandle sampler) const noexcept override;
		DX12DescriptorView ResolveTextureView(RHITextureViewHandle view) const noexcept;
		D3D12_GPU_DESCRIPTOR_HANDLE ResolveShaderVisibleDescriptor(
			RHIDescriptorHeapType heapType,
			uint32_t descriptorIndex) const noexcept;

		RHIPipelineHandle RegisterGraphicsPipeline(
			DX12PipelineState* pipelineState,
			DX12RootSignature* rootSignature) noexcept;
		bool ResolveGraphicsPipeline(
			RHIPipelineHandle pipeline,
			DX12PipelineState*& outPipelineState,
			DX12RootSignature*& outRootSignature) const noexcept;

		DX12Texture* ResolveTexture(RHITextureHandle texture) noexcept;
		const DX12Texture* ResolveTexture(RHITextureHandle texture) const noexcept;
		DX12Buffer* ResolveBuffer(RHIBufferHandle buffer) noexcept;
		const DX12Buffer* ResolveBuffer(RHIBufferHandle buffer) const noexcept;

		void RetireCompletedWork() noexcept override;
		void RetireCompletedRHIResources() noexcept;

		DX12ResourceManager* GetResourceManager() noexcept { return &m_ResourceManager; }
		const DX12ResourceManager* GetResourceManager() const noexcept { return &m_ResourceManager; }

	private:
		void InitializeWinPIX() noexcept;
		void InitializeDXGIFactory() noexcept;
		void InitializeDXGIAdapter() noexcept;
		void InitializeDebugLayer() noexcept;
		void InitializeD3D12Device() noexcept;
		void InitializeInfoQueue() noexcept;
		void InitializeMemAllocator() noexcept;
		void CheckFeatureSupport() noexcept;

	private:
		static std::wstring GetLatestWinPixGpuCapturerPath() noexcept;

	private:
		ComPtr<IDXGIFactory7> m_DxgiFactory;
		ComPtr<IDXGIAdapter1> m_DxgiAdapter;
		ComPtr<ID3D12Device10> m_D3D12Device;

		ComPtr<D3D12MA::Allocator> m_MemAllocator;

		// supported features
		FeatureSupport m_FeatureSupport;

		DX12ResourceManager m_ResourceManager;
		DX12QueueSystem* m_QueueSystem = nullptr;
		DX12DescriptorManager* m_DescriptorManager = nullptr;
		std::unique_ptr<DX12DescriptorCache> m_DescriptorCache;

		struct GraphicsPipelineBinding
		{
			DX12PipelineState* m_PipelineState = nullptr;
			DX12RootSignature* m_RootSignature = nullptr;
		};
		std::vector<GraphicsPipelineBinding> m_GraphicsPipelineBindings;
		mutable std::shared_mutex m_GraphicsPipelineMutex;

		bool m_IsInitialized = false;
	};
}
