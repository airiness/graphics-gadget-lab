#include "Precompiled.h"
#include "DevelopGui.h"
#include "DX12Device.h"
#include "DX12SwapChain.h"
#include "DX12DescriptorManager.h"
#include "DX12Descriptor.h"
#include "DX12DescriptorFreeListAllocator.h"
#include "DX12CommandQueue.h"
#include "DX12CommandList.h"

namespace gglab
{
	void DevelopGui::Initialize(const CreateInfo& createInfo) noexcept
	{
		GGLAB_ASSERT(createInfo.m_DX12Device);
		GGLAB_ASSERT(createInfo.m_DescriptorManager);

		m_DX12Device = createInfo.m_DX12Device;
		m_DescriptorManager = createInfo.m_DescriptorManager;

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
			initInfo.DSVFormat = DXGI_FORMAT_UNKNOWN;
			initInfo.SrvDescriptorHeap = m_DescriptorManager->GetCbvSrvUavDescriptorAllocator().GetHeap()->Get();
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
	}

	void DevelopGui::NewFrame() noexcept
	{
		GGLAB_ASSERT_MSG(!m_FrameOpen,
			"DevelopGui::NewFrame called twice without ending previous frame.");
		ImGui_ImplDX12_NewFrame();
		ImGui_ImplWin32_NewFrame();
		ImGui::NewFrame();
		m_FrameOpen = true;

	}

	void DevelopGui::Render(DX12CommandList* commandList, const DX12DescriptorHandle& rtv) noexcept
	{
		GGLAB_ASSERT_MSG(m_FrameOpen, "DevelopGui::Render called without NewFrame.");

		ImGui::Render();
		m_FrameOpen = false;

		DX12DescriptorView rtvs[] =
		{
			rtv.ToView()
		};
		commandList->SetRenderTargets(rtvs, nullptr);

		auto* heap = m_DescriptorManager->GetCbvSrvUavDescriptorAllocator().GetHeap();
		commandList->SetDescriptorHeap(*heap);

		ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), commandList->Get());
	}

	void DevelopGui::EndFrame() noexcept
	{
		if (!m_FrameOpen)
		{
			return;
		}

		ImGui::EndFrame();
		m_FrameOpen = false;
	}

	void DevelopGui::DescriptorAlloc(ImGui_ImplDX12_InitInfo* info,
		D3D12_CPU_DESCRIPTOR_HANDLE* outCpuHandle,
		D3D12_GPU_DESCRIPTOR_HANDLE* outGpuHandle)
	{
		auto* developGui = static_cast<DevelopGui*>(info->UserData);
		auto descriptorView = developGui->m_DescriptorManager->GetCbvSrvUavDescriptorAllocator().AllocateRaw();
		*outCpuHandle = descriptorView.m_CpuHandle;
		*outGpuHandle = descriptorView.m_GpuHandle;
	}

	void DevelopGui::DescriptorFree(ImGui_ImplDX12_InitInfo* info,
		D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle,
		D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle)
	{
		auto* developGui = static_cast<DevelopGui*>(info->UserData);

		DX12DescriptorView view
		{
			.m_CpuHandle = CD3DX12_CPU_DESCRIPTOR_HANDLE(cpuHandle),
			.m_GpuHandle = CD3DX12_GPU_DESCRIPTOR_HANDLE(gpuHandle)
		};

		developGui->m_DescriptorManager->GetCbvSrvUavDescriptorAllocator().FreeRaw(view);
	}
}