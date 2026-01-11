#pragma once
#include "RGGpuResourceAllocator.h"
#include "DX12RootSignature.h"
#include "DX12ConstantBuffer.h"
#include "DX12ViewCache.h"
#include "DX12PSOCache.h"
#include "DX12RootSignatureCache.h"
#include "DX12SwapChain.h"
#include "DX12DescriptorManager.h"
#include "RenderPassRecipeRegistry.h"
#include "ExternalResourceRegistry.h"
#include "TransferManager.h"
#include "GPUStructures.h"
#include "RenderGraph.h"
#include "DevelopGui.h"
#include "RenderContexts.h"

namespace gglab
{
	class DX12Device;
	class ShaderManager;

	class Renderer
	{
	public:
		struct CreateInfo
		{
			ShaderManager* m_ShaderManager = nullptr;
			HWND m_Hwnd = nullptr;
			uint32_t m_Width = 0;
			uint32_t m_Height = 0;

			uint32_t m_TransferManagerBufferSize = 8 * 1024 * 1024;
		};

	public:
		Renderer() noexcept = default;
		GGLAB_DELETE_COPYABLE_MOVABLE(Renderer);
		~Renderer() = default;

		void Initialize(const CreateInfo& createInfo) noexcept;
		void Finalize() noexcept;
		bool IsInitialized() const noexcept { return m_IsInitialized; }

		void Render(RenderGraph& rg, const RenderFrameContext& renderContext) noexcept;

		DX12Device* GetDevice() const noexcept { return m_Device.get(); }
		DX12SwapChain* GetSwapChain() const noexcept { return m_SwapChain.get(); }
		DX12DescriptorManager* GetDescriptorManager() const noexcept { return m_DescriptorManager.get(); }
		TransferManager* GetTransferManager() const noexcept { return m_TransferManager.get(); }
		DX12ViewCache* GetViewCache() const noexcept { return m_ViewCache.get(); }
		DX12PSOCache* GetPSOCache() const noexcept { return m_PSOCache.get(); }
		DX12RootSignatureCache* GetRootSignatureCache() const noexcept { return m_RootSignatureCache.get(); }
		RenderPassRecipeRegistry* GetRenderPassRecipeRegistry() const noexcept { return m_RenderPassRecipeRegistry.get(); }
		ExternalResourceRegistry* GetExternalResourceRegistry() const noexcept { return m_ExternalResourceRegistry.get(); }
		DevelopGui* GetDevelopGui() const noexcept { return m_DevelopGui.get(); }

		DX12RootSignature* GetCommonRootSignature() const noexcept;
		RootSignatureID GetCommonRootSignatureId() const noexcept { return m_CommonRootSignatureId; }

		const DX12ConstantBuffer<FrameCBData>* GetFrameConstantBuffer() const noexcept { return m_FrameCB.get(); }
		DX12ConstantBuffer<FrameCBData>* GetFrameConstantBuffer() noexcept { return m_FrameCB.get(); }
		const DX12RingStructuredBuffer<ObjectGPU>* GetObjectStructuredBuffer() const noexcept { return m_ObjectSB.get(); }
		DX12RingStructuredBuffer<ObjectGPU>* GetObjectStructuredBuffer() noexcept { return m_ObjectSB.get(); }
		const DX12RingStructuredBuffer<MaterialGPU>* GetMaterialStructuredBuffer() const noexcept { return m_MaterialSB.get(); }
		DX12RingStructuredBuffer<MaterialGPU>* GetMaterialStructuredBuffer() noexcept { return m_MaterialSB.get(); }

		void UpdateFrameConstants(const RenderView& view, const RenderScene& scene) noexcept;

		RenderGraph::CreateInfo CreateRenderGraphCreateInfo() const noexcept;

		void OnResize(uint32_t width, uint32_t height) noexcept;
		void OnSuspend() noexcept;
		void OnResume() noexcept;
		bool IsSuspended() const noexcept;

		DX12FencePoint GetLastSubmittedFencePoint() const noexcept { return m_LastSubmittedFencePoint; }

	private:
		void CreateCommonRootSignature() noexcept;
		void InitializeGpuBuffers() noexcept;

	private:
		std::unique_ptr<DX12Device> m_Device;
		std::unique_ptr<DX12SwapChain> m_SwapChain;
		std::unique_ptr<DX12DescriptorManager> m_DescriptorManager;
		std::unique_ptr<TransferManager> m_TransferManager;
		std::unique_ptr<RGGpuResourceAllocator> m_RGGpuAllocator;
		std::unique_ptr<DX12ViewCache> m_ViewCache;
		std::unique_ptr<DX12PSOCache> m_PSOCache;
		std::unique_ptr<DX12RootSignatureCache> m_RootSignatureCache;
		std::unique_ptr<RenderPassRecipeRegistry> m_RenderPassRecipeRegistry;
		std::unique_ptr<ExternalResourceRegistry> m_ExternalResourceRegistry;
		std::unique_ptr<DevelopGui> m_DevelopGui;

		RootSignatureID m_CommonRootSignatureId{};

		std::unique_ptr<DX12ConstantBuffer<FrameCBData>> m_FrameCB;

		std::unique_ptr<DX12RingStructuredBuffer<ObjectGPU>> m_ObjectSB;
		std::unique_ptr<DX12RingStructuredBuffer<MaterialGPU>> m_MaterialSB;

		std::atomic_bool m_IsInitialized = false;
		std::atomic_bool m_IsSuspended = false;

		DX12FencePoint m_LastSubmittedFencePoint = {};
	};
}