#pragma once
#include "Core/Platform/Win/ComTypes.h"
#include "Core/Utility/TypeUtils.h"
#include "Graphics/RHI/RHIDevice.h"
#include "Graphics/RHI/RHIFence.h"
#include "Graphics/RHI/DX12/DX12ResourceManager.h"

#include <array>
#include <memory>

namespace D3D12MA
{
	class Allocator;
}

namespace gglab
{
	enum class CommandQueueType : uint32_t
	{
		Graphics,
		Compute,
		Copy,
		Transfer,

		Count
	};

	class DX12CommandQueue;
	class DX12CommandList;
	class DX12GraphicsCommandContext;
	class DX12ComputeCommandContext;
	class DX12DescriptorFreeListAllocator;
	class DX12CommandAllocatorPool;
	class DX12FencePoint;
	class DX12Resource;
	class DX12ViewCache;
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

		DX12CommandQueue* GetCommandQueue(CommandQueueType type) const noexcept { return m_CommandQueues[utils::ToIndexChecked(type)].get(); }
		DX12CommandAllocatorPool* GetCommandAllocatorPool(CommandQueueType type) const noexcept { return m_CommandAllocatorPools[utils::ToIndexChecked(type)].get(); }

		DX12CommandList* GetGraphicsCommandList(uint32_t bufferIndex) const noexcept { return m_GraphicsCommandLists.at(bufferIndex).get(); }
		DX12CommandList* GetComputeCommandList(uint32_t bufferIndex) const noexcept { return m_ComputeCommandLists.at(bufferIndex).get(); }
		DX12GraphicsCommandContext* GetGraphicsCommandContext(uint32_t bufferIndex) const noexcept { return m_GraphicsCommandContexts.at(bufferIndex).get(); }
		DX12ComputeCommandContext* GetComputeCommandContext(uint32_t bufferIndex) const noexcept { return m_ComputeCommandContexts.at(bufferIndex).get(); }

		bool SupportRayTracing() const noexcept { return m_FeatureSupport.m_RayTracingSupported; }
		bool SupportMeshShader() const noexcept { return m_FeatureSupport.m_MeshShaderSupported; }
		bool SupportTearing() const noexcept { return m_FeatureSupport.m_TearingSupported; }
		bool SupportEnhancedBarrier() const noexcept { return m_FeatureSupport.m_EnhancedBarriers; }

		void FlushGPU() noexcept;

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
		bool IsFencePointCompleted(const RHIFencePoint& fencePoint) const noexcept;
		void RecordTextureUse(RHITextureHandle texture, const DX12FencePoint& fencePoint) noexcept;
		void RecordTextureUse(RHITextureHandle texture, const RHIFencePoint& fencePoint) noexcept;
		void RecordBufferUse(RHIBufferHandle buffer, const DX12FencePoint& fencePoint) noexcept;
		void RecordBufferUse(RHIBufferHandle buffer, const RHIFencePoint& fencePoint) noexcept;
		void SetViewCache(DX12ViewCache* viewCache) noexcept;

		bool IsAlive(RHITextureHandle texture) const noexcept override;
		bool IsAlive(RHIBufferHandle buffer) const noexcept override;
		bool IsAlive(RHISamplerHandle sampler) const noexcept override;
		RHIDescriptorHandle GetTextureViewDescriptor(RHITextureViewHandle view) const noexcept override;
		RHIDescriptorHandle GetBufferViewDescriptor(RHIBufferViewHandle view) const noexcept override;
		RHIDescriptorHandle GetSamplerDescriptor(RHISamplerHandle sampler) const noexcept override;

		DX12Texture* ResolveTexture(RHITextureHandle texture) noexcept;
		const DX12Texture* ResolveTexture(RHITextureHandle texture) const noexcept;
		DX12Buffer* ResolveBuffer(RHIBufferHandle buffer) noexcept;
		const DX12Buffer* ResolveBuffer(RHIBufferHandle buffer) const noexcept;

		void RetireCompletedWork() noexcept override;
		void RetireCompletedRHIResources() noexcept;

		DX12ResourceManager* GetResourceManager() noexcept { return &m_ResourceManager; }
		const DX12ResourceManager* GetResourceManager() const noexcept { return &m_ResourceManager; }

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

		// Command Queues
		std::array<std::unique_ptr<DX12CommandQueue>, utils::EnumCount<CommandQueueType>()> m_CommandQueues;

		// Command Allocator Pools
		std::array<std::unique_ptr<DX12CommandAllocatorPool>, utils::EnumCount<CommandQueueType>()> m_CommandAllocatorPools;

		std::array<std::unique_ptr<DX12CommandList>, BufferCount> m_GraphicsCommandLists;
		std::array<std::unique_ptr<DX12CommandList>, BufferCount> m_ComputeCommandLists;
		std::array<std::unique_ptr<DX12GraphicsCommandContext>, BufferCount> m_GraphicsCommandContexts;
		std::array<std::unique_ptr<DX12ComputeCommandContext>, BufferCount> m_ComputeCommandContexts;

		// supported features
		FeatureSupport m_FeatureSupport;

		DX12ResourceManager m_ResourceManager;
		DX12ViewCache* m_ViewCache = nullptr;

		bool m_IsInitialized = false;
	};
}
