#include "Core/Precompiled.h"
#include "DevTools/DevelopGui/Backends/DX12/DevelopGuiDX12Backend.h"
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
	bool DevelopGuiDX12Backend::Initialize(const CreateInfo& createInfo) noexcept
	{
		GGLAB_ASSERT_MSG(m_State == State::Inactive,
			"DevelopGuiDX12Backend is already initialized.");
		if (m_State != State::Inactive)
		{
			return false;
		}

		if (!createInfo.m_RHIContext || !createInfo.m_NativeWindowHandle)
		{
			GGLAB_LOG_GRAPHICS_ERROR(
				"DevelopGuiDX12Backend requires an RHI context and a native window handle.");
			return false;
		}

		auto* dx12Context = dynamic_cast<DX12Context*>(createInfo.m_RHIContext);
		if (!dx12Context)
		{
			GGLAB_LOG_GRAPHICS_ERROR(
				"DevelopGuiDX12Backend requires the DX12 RHI backend.");
			return false;
		}

		m_DX12Device = &dx12Context->GetDX12Device();
		m_DescriptorManager = &dx12Context->GetDescriptorManager();
		auto& swapChain = dx12Context->GetSwapChain();

		IMGUI_CHECKVERSION();
		if (!ImGui::CreateContext())
		{
			GGLAB_LOG_GRAPHICS_ERROR("Failed to create the ImGui context.");
			m_DX12Device = nullptr;
			m_DescriptorManager = nullptr;
			return false;
		}

		ImGuiIO& io = ImGui::GetIO();

		io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
		io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;

		ImGui::StyleColorsDark();

		if (!ImGui_ImplWin32_Init(static_cast<HWND>(createInfo.m_NativeWindowHandle)))
		{
			GGLAB_LOG_GRAPHICS_ERROR("Failed to initialize the ImGui Win32 backend.");
			ImGui::DestroyContext();
			m_DX12Device = nullptr;
			m_DescriptorManager = nullptr;
			return false;
		}

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

		if (!ImGui_ImplDX12_Init(&initInfo))
		{
			GGLAB_LOG_GRAPHICS_ERROR("Failed to initialize the ImGui DX12 backend.");
			if (ImGui::GetIO().BackendRendererUserData)
			{
				ImGui_ImplDX12_Shutdown();
			}
			ImGui_ImplWin32_Shutdown();
			ImGui::DestroyContext();
			m_DX12Device = nullptr;
			m_DescriptorManager = nullptr;
			return false;
		}

		m_State = State::Active;
		return true;
	}

	void DevelopGuiDX12Backend::Finalize() noexcept
	{
		if (m_State == State::Inactive)
		{
			return;
		}

		EndFrame();
		ImGui_ImplDX12_Shutdown();
		ImGui_ImplWin32_Shutdown();

		ImGui::DestroyContext();
		m_DX12Device = nullptr;
		m_DescriptorManager = nullptr;
		m_State = State::Inactive;
	}

	bool DevelopGuiDX12Backend::NewFrame() noexcept
	{
		GGLAB_ASSERT_MSG(m_State != State::FrameOpen,
			"DevelopGuiDX12Backend::NewFrame called twice without ending previous frame.");
		if (m_State != State::Active)
		{
			return false;
		}

		ImGui_ImplDX12_NewFrame();
		ImGui_ImplWin32_NewFrame();
		ImGui::NewFrame();
		m_State = State::FrameOpen;
		return true;
	}

	void DevelopGuiDX12Backend::RenderDrawData(
		RHIGraphicsCommandContext* commandContext,
		RHITextureViewHandle renderTarget) noexcept
	{
		GGLAB_ASSERT_MSG(m_State == State::FrameOpen,
			"DevelopGuiDX12Backend::RenderDrawData called without NewFrame.");
		if (m_State != State::FrameOpen)
		{
			return;
		}

		auto* dx12Context = dynamic_cast<DX12GraphicsCommandContext*>(commandContext);
		GGLAB_ASSERT_NOT_NULL(dx12Context);
		if (!dx12Context)
		{
			return;
		}

		ImGui::Render();
		m_State = State::Active;

		commandContext->SetRenderTargets(std::span<const RHITextureViewHandle>(&renderTarget, 1));

		auto* heap = m_DescriptorManager->GetFreeListAllocator(DX12DescriptorManager::AllocatorType::DevelopGuiSrv)->GetHeap();
		dx12Context->GetCommandList()->SetDescriptorHeap(*heap);

		ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), dx12Context->Get());
	}

	void DevelopGuiDX12Backend::EndFrame() noexcept
	{
		if (m_State != State::FrameOpen)
		{
			return;
		}

		ImGui::EndFrame();
		m_State = State::Active;
	}

	ImTextureID DevelopGuiDX12Backend::ResolveTextureId(
		RHIDescriptorHandle descriptor) const noexcept
	{
		if (!IsActive() ||
			!m_DescriptorManager ||
			!descriptor.IsValid() ||
			descriptor.m_HeapType != RHIDescriptorHeapType::CbvSrvUav)
		{
			return {};
		}

		auto* heap = m_DescriptorManager->GetHeap(
			DX12DescriptorManager::HeapType::CbvSrvUav);
		return heap ? static_cast<ImTextureID>(heap->GpuHandleAt(descriptor.m_Index).ptr) : ImTextureID{};
	}

	void DevelopGuiDX12Backend::DescriptorAlloc(ImGui_ImplDX12_InitInfo* info,
		D3D12_CPU_DESCRIPTOR_HANDLE* outCpuHandle,
		D3D12_GPU_DESCRIPTOR_HANDLE* outGpuHandle)
	{
		auto* backend = static_cast<DevelopGuiDX12Backend*>(info->UserData);
		auto descriptorView = backend->m_DescriptorManager->AllocateDevelopGuiSrvView();
		*outCpuHandle = descriptorView.m_CpuHandle;
		*outGpuHandle = descriptorView.m_GpuHandle;
	}

	void DevelopGuiDX12Backend::DescriptorFree(ImGui_ImplDX12_InitInfo* info,
		D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle,
		D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle)
	{
		GGLAB_UNUSED(cpuHandle);
		auto* backend = static_cast<DevelopGuiDX12Backend*>(info->UserData);
		backend->m_DescriptorManager->DeferFreeDevelopGuiSrvInFrame(gpuHandle);
	}
}
