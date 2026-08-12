#include "DevTools/DevelopGui/Backends/DX12/DevelopGuiDX12RenderBackend.h"
#include "Core/Log/LogMacros.h"
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

#include <imgui.h>

namespace gglab
{
	bool DevelopGuiDX12RenderBackend::Initialize(RHIContext& context) noexcept
	{
		if (m_IsInitialized)
		{
			return false;
		}

		auto* dx12Context = dynamic_cast<DX12Context*>(&context);
		if (!dx12Context)
		{
			GGLAB_LOG_GRAPHICS_ERROR(
				"DevelopGui DX12 render backend requires the DX12 RHI backend.");
			return false;
		}

		m_DX12Device = &dx12Context->GetDX12Device();
		m_DescriptorManager = &dx12Context->GetDescriptorManager();
		auto& swapChain = dx12Context->GetSwapChain();

		ImGui_ImplDX12_InitInfo initInfo{};
		initInfo.Device = m_DX12Device->Get();
		initInfo.CommandQueue =
			dx12Context->GetQueueSystem().GetQueue(DX12QueueType::Graphics).Get();
		initInfo.NumFramesInFlight = context.GetFrameSlotCount();
		initInfo.RTVFormat = ToDXGIFormat(swapChain.GetFormat());
		initInfo.DSVFormat = DXGI_FORMAT_UNKNOWN;
		initInfo.SrvDescriptorHeap = m_DescriptorManager
			->GetFreeListAllocator(DX12DescriptorManager::AllocatorType::DevelopGuiSrv)
			->GetHeap()
			->Get();
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
			m_DX12Device = nullptr;
			m_DescriptorManager = nullptr;
			return false;
		}

		m_IsInitialized = true;
		return true;
	}

	void DevelopGuiDX12RenderBackend::Finalize() noexcept
	{
		if (!m_IsInitialized)
		{
			return;
		}

		ImGui_ImplDX12_Shutdown();
		m_DX12Device = nullptr;
		m_DescriptorManager = nullptr;
		m_IsInitialized = false;
	}

	void DevelopGuiDX12RenderBackend::NewFrame() noexcept
	{
		GGLAB_ASSERT(m_IsInitialized);
		if (m_IsInitialized)
		{
			ImGui_ImplDX12_NewFrame();
		}
	}

	void DevelopGuiDX12RenderBackend::RenderDrawData(
		RHIGraphicsCommandContext* commandContext, RHITextureViewHandle renderTarget) noexcept
	{
		GGLAB_ASSERT(m_IsInitialized);
		if (!m_IsInitialized)
		{
			return;
		}

		auto* dx12Context = dynamic_cast<DX12GraphicsCommandContext*>(commandContext);
		GGLAB_ASSERT_NOT_NULL(dx12Context);
		if (!dx12Context)
		{
			return;
		}

		const RHIRenderingAttachment colorAttachment{ .m_View = renderTarget };
		commandContext->BeginRendering({ .m_ColorAttachments =
			std::span<const RHIRenderingAttachment>(&colorAttachment, 1) });

		auto* heap = m_DescriptorManager
			->GetFreeListAllocator(DX12DescriptorManager::AllocatorType::DevelopGuiSrv)
			->GetHeap();
		dx12Context->GetCommandList()->SetDescriptorHeap(*heap);

		ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), dx12Context->Get());
	}

	ImTextureID DevelopGuiDX12RenderBackend::ResolveTextureId(
		RHIDescriptorHandle descriptor) const noexcept
	{
		if (!m_IsInitialized || !m_DescriptorManager || !descriptor.IsValid() ||
			descriptor.m_HeapType != RHIDescriptorHeapType::CbvSrvUav)
		{
			return {};
		}

		auto* heap = m_DescriptorManager->GetHeap(DX12DescriptorManager::HeapType::CbvSrvUav);
		return heap ? static_cast<ImTextureID>(heap->GpuHandleAt(descriptor.m_Index).ptr)
			: ImTextureID{};
	}

	void DevelopGuiDX12RenderBackend::DescriptorAlloc(ImGui_ImplDX12_InitInfo* info,
		D3D12_CPU_DESCRIPTOR_HANDLE* outCpuHandle, D3D12_GPU_DESCRIPTOR_HANDLE* outGpuHandle)
	{
		auto* backend = static_cast<DevelopGuiDX12RenderBackend*>(info->UserData);
		auto descriptorView = backend->m_DescriptorManager->AllocateDevelopGuiSrvView();
		*outCpuHandle = descriptorView.m_CpuHandle;
		*outGpuHandle = descriptorView.m_GpuHandle;
	}

	void DevelopGuiDX12RenderBackend::DescriptorFree(ImGui_ImplDX12_InitInfo* info,
		D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle, D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle)
	{
		GGLAB_UNUSED(cpuHandle);
		auto* backend = static_cast<DevelopGuiDX12RenderBackend*>(info->UserData);
		backend->m_DescriptorManager->DeferFreeDevelopGuiSrvInFrame(gpuHandle);
	}
}
