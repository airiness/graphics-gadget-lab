#pragma once
#include "RGGpuResourceAllocator.h"
#include "DX12RootSignature.h"
#include "DX12ConstantBuffer.h"
#include "DX12ViewCache.h"
#include "DX12PSOCache.h"
#include "DX12RootSignatureCache.h"
#include "RenderPassRecipeRegistry.h"
#include "TransferManager.h"
#include "GPUStructures.h"
#include "Camera.h"

namespace gglab
{
	class DX12Device;
	class Renderer
	{
	public:
		Renderer() noexcept;
		GGLAB_DELETE_COPYABLE_MOVABLE(Renderer);
		~Renderer() = default;

		void Initialize() noexcept;
		void Update() noexcept;
		void Render() noexcept;
		void Finalize() noexcept;

		bool IsInitialized() const noexcept { return m_IsInitialized; }

		DX12Device* GetDevice() const noexcept { return m_Device.get(); }
		DX12ConstantBuffer<FrameCBData>* GetFrameConstantBuffer() const noexcept { return m_FrameCB.get(); }
		DX12ViewCache* GetViewCache() const noexcept { return m_ViewCache.get(); }
		DX12PSOCache* GetPSOCache() const noexcept { return m_PSOCache.get(); }
		DX12RootSignatureCache* GetRootSignatureCache() const noexcept { return m_RootSignatureCache.get(); }
		RenderPassRecipeRegistry* GetRenderPassRecipeRegistry() const noexcept { return m_RenderPassRecipeRegistry.get(); }

		DX12RootSignature* GetCommonRootSignature() const noexcept;
		RootSignatureId GetCommonRootSignatureId() const noexcept { return m_CommonRootSignatureId; }

	private:
		void CreateCamera() noexcept;
		void CreateCommonRootSignature() noexcept;

		void InitializeGpuBuffers() noexcept;

		void UpdateGpuBuffers() noexcept;

		void UpdateFrameConstantBuffer() noexcept;
		void UpdateStructuredBuffers() noexcept;

	private:
		std::unique_ptr<DX12Device> m_Device;
		std::unique_ptr<Camera> m_Camera;
		std::unique_ptr<RGGpuResourceAllocator> m_RGGpuAllocator;
		std::unique_ptr<DX12ViewCache> m_ViewCache;
		std::unique_ptr<DX12PSOCache> m_PSOCache;
		std::unique_ptr<DX12RootSignatureCache> m_RootSignatureCache;
		std::unique_ptr<RenderPassRecipeRegistry> m_RenderPassRecipeRegistry;
		std::unique_ptr<TransferManager> m_TransferManager;

		std::unique_ptr<DX12ConstantBuffer<FrameCBData>> m_FrameCB;
		std::unique_ptr<DX12RingStructuredBuffer<ObjectGPU>> m_ObjectSB;
		std::unique_ptr<DX12RingStructuredBuffer<MaterialGPU>> m_MaterialSB;

		RootSignatureId m_CommonRootSignatureId{};

		std::atomic_bool m_IsInitialized = false;
	};
}
