#pragma once
#include "Graphics/RHI/RHIDescriptor.h"
#include "Graphics/RHI/RHITexture.h"

namespace gglab
{
	class RHIContext;
	class DX12Device;
	class DX12DescriptorManager;
	class RHIGraphicsCommandContext;

	class DevelopGuiBackend
	{
	public:
		struct CreateInfo
		{
			HWND m_Hwnd = nullptr;
			RHIContext* m_RHIContext = nullptr;
		};

	public:
		DevelopGuiBackend() noexcept = default;
		GGLAB_DELETE_COPYABLE_MOVABLE(DevelopGuiBackend);
		~DevelopGuiBackend() = default;

		void Initialize(const CreateInfo& createInfo) noexcept;
		void Finalize() noexcept;

		void NewFrame() noexcept;
		void RenderDrawData(
			RHIGraphicsCommandContext* commandContext,
			RHITextureViewHandle renderTarget) noexcept;
		void EndFrame() noexcept;
		[[nodiscard]] ImTextureID ResolveTextureId(
			RHIDescriptorHandle descriptor) const noexcept;

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
		bool m_FrameOpen = false;
	};
}
