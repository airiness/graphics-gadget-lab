#include "Precompiled.h"
#include "DevelopGui.h"
#include "DX12Device.h"
#include "DX12SwapChain.h"
#include "DX12Descriptor.h"
#include "DX12DescriptorFreeListAllocator.h"
#include "DX12CommandQueue.h"

namespace gglab
{
	void DevelopGui::Initialize(const CreateInfo& createInfo) noexcept
	{
		m_DX12Device = createInfo.m_DX12Device;

		// Descriptor heap initialize
		{
			DX12DescriptorHeap::CreateInfo heapCreateInfo{};
			heapCreateInfo.m_DX12Device = m_DX12Device;
			heapCreateInfo.m_Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
			heapCreateInfo.m_Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
			heapCreateInfo.m_DescriptorCount = 256;
			m_DescriptorAllocator = std::make_unique<DX12DescriptorFreeListAllocator>(heapCreateInfo);
		}

		// ImGui initialize
		{
			IMGUI_CHECKVERSION();
			ImGui::CreateContext();
			ImGuiIO& io = ImGui::GetIO();

			io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
			io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;

			ImGui::StyleColorsDark();

			ImGui_ImplWin32_Init(createInfo.m_Hwnd);

			ImGui_ImplDX12_InitInfo initInfo{};
			initInfo.Device = m_DX12Device->Get();
			initInfo.CommandQueue = m_DX12Device->GetGraphicsCommandQueue()->Get();
			initInfo.NumFramesInFlight = DX12Device::GetBufferCount();
			initInfo.RTVFormat = createInfo.m_SwapChain->GetFormat();
			initInfo.DSVFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
			initInfo.SrvDescriptorHeap = m_DescriptorAllocator->GetHeap()->Get();
			initInfo.SrvDescriptorAllocFn = DescriptorAlloc;
			initInfo.SrvDescriptorFreeFn = DescriptorFree;
			initInfo.UserData = this;

			ImGui_ImplDX12_Init(&initInfo);
		}
	}

	void DevelopGui::Finalize() noexcept
	{
		ImGui_ImplDX12_Shutdown();
		ImGui_ImplWin32_Shutdown();

		ImGui::DestroyContext();
		m_DescriptorAllocator.reset();
	}

	void DevelopGui::NewFrame() noexcept
	{
		ImGui_ImplDX12_NewFrame();
		ImGui_ImplWin32_NewFrame();
		ImGui::NewFrame();
	}

	void DevelopGui::Render(DX12CommandList* commandList, const DX12Descriptor& rtv) noexcept
	{

	}

	void DevelopGui::DescriptorAlloc(ImGui_ImplDX12_InitInfo* info, 
		D3D12_CPU_DESCRIPTOR_HANDLE* outCpuHandle, 
		D3D12_GPU_DESCRIPTOR_HANDLE* outGpuHandle)
	{
		auto* developGui = static_cast<DevelopGui*>(info->UserData);
		auto descriptor = developGui->m_DescriptorAllocator->Allocate(1);
		//outCpuHandle = &descriptor.CpuHandle();
		//outGpuHandle = &descriptor.GpuHandle();


	}

	void DevelopGui::DescriptorFree(ImGui_ImplDX12_InitInfo* info, 
		D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle, 
		D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle)
	{
	}
}