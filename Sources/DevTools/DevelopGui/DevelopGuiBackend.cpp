#include "Core/Precompiled.h"
#include "DevTools/DevelopGui/DevelopGuiBackend.h"
#include "Graphics/DX12/DX12Device.h"
#include "Graphics/DX12/DX12SwapChain.h"
#include "Graphics/DX12/Descriptor/DX12DescriptorManager.h"
#include "Graphics/DX12/Descriptor/DX12DescriptorTypes.h"
#include "Graphics/DX12/Descriptor/DX12DescriptorHeap.h"
#include "Graphics/DX12/Descriptor/DX12DescriptorFreeListAllocator.h"
#include "Graphics/DX12/DX12CommandQueue.h"
#include "Graphics/DX12/DX12CommandList.h"

namespace gglab
{
	void DevelopGuiBackend::Initialize(const CreateInfo& createInfo) noexcept
	{
		GGLAB_ASSERT(createInfo.m_DX12Device);
		GGLAB_ASSERT(createInfo.m_DescriptorManager);

		m_DX12Device = createInfo.m_DX12Device;
		m_DescriptorManager = createInfo.m_DescriptorManager;

		IMGUI_CHECKVERSION();
		ImGui::CreateContext();
		ImGuiIO& io = ImGui::GetIO();

		io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
		io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;

		ImGui::StyleColorsDark();

		ImGui_ImplWin32_Init(createInfo.m_Hwnd);

		ImGui_ImplDX12_InitInfo initInfo{};
		initInfo.Device = m_DX12Device->Get();
		initInfo.CommandQueue = m_DX12Device->GetCommandQueue(CommandQueueType::Graphics)->Get();
		initInfo.NumFramesInFlight = DX12Device::GetBufferCount();
		initInfo.RTVFormat = createInfo.m_SwapChain->GetFormat();
		initInfo.DSVFormat = DXGI_FORMAT_UNKNOWN;
		initInfo.SrvDescriptorHeap =
			m_DescriptorManager->GetFreeListAllocator(DX12DescriptorManager::AllocatorType::DevelopGuiSrv)->GetHeap()->Get();
		initInfo.SrvDescriptorAllocFn = DescriptorAlloc;
		initInfo.SrvDescriptorFreeFn = DescriptorFree;
		initInfo.UserData = this;

		ImGui_ImplDX12_Init(&initInfo);
	}

	void DevelopGuiBackend::Finalize() noexcept
	{
		ImGui_ImplDX12_Shutdown();
		ImGui_ImplWin32_Shutdown();

		ImGui::DestroyContext();
	}

	void DevelopGuiBackend::NewFrame() noexcept
	{
		GGLAB_ASSERT_MSG(!m_FrameOpen,
			"DevelopGuiBackend::NewFrame called twice without ending previous frame.");
		ImGui_ImplDX12_NewFrame();
		ImGui_ImplWin32_NewFrame();
		ImGui::NewFrame();
		m_FrameOpen = true;
	}

	void DevelopGuiBackend::RenderDrawData(DX12CommandList* commandList, const DX12DescriptorView& rtv) noexcept
	{
		GGLAB_ASSERT_MSG(m_FrameOpen, "DevelopGuiBackend::RenderDrawData called without NewFrame.");

		ImGui::Render();
		m_FrameOpen = false;

		commandList->SetRenderTarget(rtv, {});

		auto* heap = m_DescriptorManager->GetFreeListAllocator(DX12DescriptorManager::AllocatorType::DevelopGuiSrv)->GetHeap();
		commandList->SetDescriptorHeap(*heap);

		ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), commandList->Get());
	}

	void DevelopGuiBackend::EndFrame() noexcept
	{
		if (!m_FrameOpen)
		{
			return;
		}

		ImGui::EndFrame();
		m_FrameOpen = false;
	}

	void DevelopGuiBackend::DescriptorAlloc(ImGui_ImplDX12_InitInfo* info,
		D3D12_CPU_DESCRIPTOR_HANDLE* outCpuHandle,
		D3D12_GPU_DESCRIPTOR_HANDLE* outGpuHandle)
	{
		auto* backend = static_cast<DevelopGuiBackend*>(info->UserData);
		auto descriptorView = backend->m_DescriptorManager->AllocateDevelopGuiSrvView();
		*outCpuHandle = descriptorView.m_CpuHandle;
		*outGpuHandle = descriptorView.m_GpuHandle;
	}

	void DevelopGuiBackend::DescriptorFree(ImGui_ImplDX12_InitInfo* info,
		D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle,
		D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle)
	{
		auto* backend = static_cast<DevelopGuiBackend*>(info->UserData);
		backend->m_DescriptorManager->DeferFreeDevelopGuiSrvInFrame(gpuHandle);
	}
}
