#pragma once
#include "Graphics/DevelopGui/DevelopGuiRegistry.h"

namespace gglab
{
	struct DX12DescriptorView;
	struct DevelopGuiContext;

	class DX12Device;
	class DX12SwapChain;
	class DX12DescriptorHeap;
	class DX12CommandList;
	class DX12DescriptorFreeListAllocator;
	class DX12DescriptorManager;

	class DevelopGui
	{
	public:
		struct CreateInfo
		{
			HWND m_Hwnd = nullptr;
			DX12Device* m_DX12Device = nullptr;
			DX12SwapChain* m_SwapChain = nullptr;
			DX12DescriptorManager* m_DescriptorManager = nullptr;
		};

	public:
		DevelopGui() noexcept = default;
		GGLAB_DELETE_COPYABLE_MOVABLE(DevelopGui);
		~DevelopGui() = default;

		void Initialize(const CreateInfo& createInfo) noexcept;
		void Finalize() noexcept;

		void NewFrame() noexcept;
		void Render(DX12CommandList* commandList, const DX12DescriptorView& rtv) noexcept;
		void EndFrame() noexcept;

		void Draw(DevelopGuiContext& context) noexcept;

		DevelopGuiRegistry& GetRegistry() noexcept { return m_Registry; }

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

		DevelopGuiRegistry m_Registry;

		bool m_ShowImGuiDemo = false;
		bool m_FrameOpen = false;
	};
}