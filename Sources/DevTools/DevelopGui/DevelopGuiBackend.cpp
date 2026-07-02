#include "Core/Precompiled.h"
#include "DevTools/DevelopGui/DevelopGuiBackend.h"
#include "Graphics/RHI/RHIContext.h"
#include "Graphics/RHI/DX12/DX12Context.h"
#include "Graphics/RHI/DX12/DX12QueueSystem.h"
#include "Graphics/RHI/DX12/DX12Device.h"
#include "Graphics/RHI/DX12/Descriptor/DX12DescriptorManager.h"
#include "Graphics/RHI/DX12/Descriptor/DX12DescriptorTypes.h"
#include "Graphics/RHI/DX12/Descriptor/DX12DescriptorHeap.h"
#include "Graphics/RHI/DX12/Descriptor/DX12DescriptorFreeListAllocator.h"
#include "Graphics/RHI/DX12/DX12CommandQueue.h"
#include "Graphics/RHI/DX12/DX12CommandList.h"
#include "Graphics/RHI/DX12/DX12CommandContext.h"
#include "Graphics/Utility/DXGIFormatUtils.h"

namespace gglab
{
	void DevelopGuiBackend::Initialize(const CreateInfo& createInfo) noexcept
	{
		GGLAB_ASSERT(createInfo.m_RHIContext);
		auto* dx12Context = dynamic_cast<DX12Context*>(createInfo.m_RHIContext);
		GGLAB_ASSERT_MSG(dx12Context != nullptr,
			"DevelopGuiBackend currently requires the DX12 RHI backend.");

		m_DX12Device = &dx12Context->GetDX12Device();
		m_DescriptorManager = &dx12Context->GetDescriptorManager();
		auto& swapChain = dx12Context->GetSwapChain();

		IMGUI_CHECKVERSION();
		ImGui::CreateContext();
		ImGuiIO& io = ImGui::GetIO();

		io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
		io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;

		ImGui::StyleColorsDark();

		ImGui_ImplWin32_Init(createInfo.m_Hwnd);

		ImGui_ImplDX12_InitInfo initInfo{};
		initInfo.Device = m_DX12Device->Get();
		initInfo.CommandQueue = dx12Context->GetQueueSystem().GetQueue(DX12QueueType::Graphics).Get();
		initInfo.NumFramesInFlight = swapChain.GetBufferCount();
		initInfo.RTVFormat = ToDXGIFormat(swapChain.GetFormat());
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

	void DevelopGuiBackend::RenderDrawData(
		RHIGraphicsCommandContext* commandContext,
		RHITextureViewHandle renderTarget) noexcept
	{
		GGLAB_ASSERT_MSG(m_FrameOpen, "DevelopGuiBackend::RenderDrawData called without NewFrame.");
		auto* dx12Context = dynamic_cast<DX12GraphicsCommandContext*>(commandContext);
		GGLAB_ASSERT_NOT_NULL(dx12Context);
		if (!dx12Context)
		{
			return;
		}

		ImGui::Render();
		m_FrameOpen = false;

		commandContext->SetRenderTargets(std::span<const RHITextureViewHandle>(&renderTarget, 1));

		auto* heap = m_DescriptorManager->GetFreeListAllocator(DX12DescriptorManager::AllocatorType::DevelopGuiSrv)->GetHeap();
		dx12Context->GetCommandList()->SetDescriptorHeap(*heap);

		ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), dx12Context->Get());
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

	ImTextureID DevelopGuiBackend::ResolveTextureId(
		RHIDescriptorHandle descriptor) const noexcept
	{
		if (!m_DescriptorManager ||
			!descriptor.IsValid() ||
			descriptor.m_HeapType != RHIDescriptorHeapType::CbvSrvUav)
		{
			return {};
		}

		auto* heap = m_DescriptorManager->GetHeap(
			DX12DescriptorManager::HeapType::CbvSrvUav);
		return heap ? static_cast<ImTextureID>(heap->GpuHandleAt(descriptor.m_Index).ptr) : ImTextureID{};
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
