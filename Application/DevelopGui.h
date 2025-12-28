#pragma once

namespace gglab
{
	class DX12Device;
	class DX12SwapChain;
	class DX12Descriptor;
	class DX12DescriptorHeap;
	class DX12CommandList;
	class DX12DescriptorFreeListAllocator;
	class DevelopGui
	{
	public:
		struct CreateInfo
		{
			HWND m_Hwnd = nullptr;
			DX12Device* m_DX12Device = nullptr;
			DX12SwapChain* m_SwapChain = nullptr;
		};

	public:
		DevelopGui() noexcept = default;
		GGLAB_DELETE_COPYABLE_MOVABLE(DevelopGui);
		~DevelopGui() = default;

		void Initialize(const CreateInfo& createInfo) noexcept;
		void Finalize() noexcept;

		void NewFrame() noexcept;
		void Render(DX12CommandList* commandList, const DX12Descriptor& rtv) noexcept;

	private:
		static void DescriptorAlloc(ImGui_ImplDX12_InitInfo* info,
			D3D12_CPU_DESCRIPTOR_HANDLE* outCpuHandle,
			D3D12_GPU_DESCRIPTOR_HANDLE* outGpuHandle);

		static void DescriptorFree(ImGui_ImplDX12_InitInfo* info,
			D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle,
			D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle);

	private:
		DX12Device* m_DX12Device = nullptr;
		std::unique_ptr<DX12DescriptorFreeListAllocator> m_DescriptorAllocator;
	};
}