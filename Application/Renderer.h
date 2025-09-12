#pragma once
#include "RendererConstant.h"
#include "RGGpuResourceAllocator.h"
#include "RenderPassTexColor.h"
#include "DX12RootSignature.h"
#include "DX12ConstantBuffer.h"
#include "ViewCache.h"
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
		DX12RootSignature* GetCommonRootSignature() const noexcept { return m_CommonRootSignature.get(); }
		DX12ConstantBuffer<GlobalConstantBuffer>* GetGlobalConstantBuffer() const noexcept { return m_GlobalConstantBuffer.get(); }
		ViewCache* GetViewCache() const noexcept { return m_ViewCache.get(); }
	private:
		void CreateRenderObjects() noexcept;
		void CreateCamera() noexcept;
		void CreateCommonRootSignature() noexcept;

		void UpdateGpuBuffers() noexcept;
		void UpdateGlobalConstantBuffer() noexcept;

	private:
		std::unique_ptr<DX12Device> m_Device;
		std::unique_ptr<Camera> m_Camera;
		std::unique_ptr<DX12ConstantBuffer<GlobalConstantBuffer>> m_GlobalConstantBuffer;
		std::unique_ptr<DX12RootSignature> m_CommonRootSignature;
		std::unique_ptr<RGGpuResourceAllocator> m_RGGpuAllocator;
		std::unique_ptr<RenderPassTexColor> m_TexColorPass;
		std::unique_ptr<ViewCache> m_ViewCache;

		std::atomic_bool m_IsInitialized = false;
	};
}
