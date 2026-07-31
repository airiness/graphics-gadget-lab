#pragma once
#include "DevTools/DevelopGui/DevelopGuiRenderBackend.h"

#include <backends/imgui_impl_dx12.h>
#include <d3d12.h>

namespace gglab
{
	class DX12DescriptorManager;
	class DX12Device;

	class DevelopGuiDX12RenderBackend final : public DevelopGuiRenderBackend
	{
	public:
		DevelopGuiDX12RenderBackend() noexcept = default;
		GGLAB_DELETE_COPYABLE_MOVABLE(DevelopGuiDX12RenderBackend);
		~DevelopGuiDX12RenderBackend() override = default;

		[[nodiscard]] bool Initialize(RHIContext& context) noexcept override;
		void Finalize() noexcept override;
		void NewFrame() noexcept override;
		void RenderDrawData(RHIGraphicsCommandContext* commandContext,
			RHITextureViewHandle renderTarget) noexcept override;
		[[nodiscard]] ImTextureID ResolveTextureId(
			RHIDescriptorHandle descriptor) const noexcept override;

	private:
		static void DescriptorAlloc(ImGui_ImplDX12_InitInfo* info,
			D3D12_CPU_DESCRIPTOR_HANDLE* outCpuHandle, D3D12_GPU_DESCRIPTOR_HANDLE* outGpuHandle);

		static void DescriptorFree(ImGui_ImplDX12_InitInfo* info,
			D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle, D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle);

	private:
		DX12Device* m_DX12Device = nullptr;
		DX12DescriptorManager* m_DescriptorManager = nullptr;
		bool m_IsInitialized = false;
	};
}
