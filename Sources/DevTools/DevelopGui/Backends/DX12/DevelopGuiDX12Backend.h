#pragma once
#include "DevTools/DevelopGui/DevelopGuiBackend.h"

#include <backends/imgui_impl_dx12.h>
#include <d3d12.h>

namespace gglab
{
	class DX12DescriptorManager;
	class DX12Device;

	class DevelopGuiDX12Backend final : public DevelopGuiBackend
	{
	public:
		DevelopGuiDX12Backend() noexcept = default;
		GGLAB_DELETE_COPYABLE_MOVABLE(DevelopGuiDX12Backend);
		~DevelopGuiDX12Backend() override = default;

		[[nodiscard]] bool Initialize(const CreateInfo& createInfo) noexcept override;
		void Finalize() noexcept override;

		[[nodiscard]] bool NewFrame() noexcept override;
		void RenderDrawData(
			RHIGraphicsCommandContext* commandContext,
			RHITextureViewHandle renderTarget) noexcept override;
		void EndFrame() noexcept override;
		[[nodiscard]] State GetState() const noexcept override { return m_State; }
		[[nodiscard]] ImTextureID ResolveTextureId(
			RHIDescriptorHandle descriptor) const noexcept override;

	private:
		static void DescriptorAlloc(ImGui_ImplDX12_InitInfo* info,
			D3D12_CPU_DESCRIPTOR_HANDLE* outCpuHandle,
			D3D12_GPU_DESCRIPTOR_HANDLE* outGpuHandle);

		static void DescriptorFree(ImGui_ImplDX12_InitInfo* info,
			D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle,
			D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle);

	private:
		DX12Device* m_DX12Device = nullptr;
		DX12DescriptorManager* m_DescriptorManager = nullptr;
		State m_State = State::Inactive;
	};
}
