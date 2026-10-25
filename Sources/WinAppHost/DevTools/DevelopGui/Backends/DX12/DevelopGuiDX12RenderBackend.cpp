#include "DevTools/DevelopGui/Backends/DX12/DevelopGuiDX12RenderBackend.h"
#include "GGLabRuntime/Core/Log/LogMacros.h"
#include "GGLabRuntime/Graphics/RHI/RHIContext.h"

#include <imgui.h>

namespace gglab
{
	bool DevelopGuiDX12RenderBackend::Initialize(RHIContext& context) noexcept
	{
		if (m_IsInitialized)
		{
			return false;
		}

		m_Interop = CreateDX12GuiInterop(context);
		if (!m_Interop)
		{
			GGLAB_LOG_GRAPHICS_ERROR(
				"DevelopGui DX12 render backend requires the DX12 RHI backend.");
			return false;
		}

		const auto native = m_Interop->GetNativeInfo();

		ImGui_ImplDX12_InitInfo initInfo{};
		initInfo.Device = native.m_Device;
		initInfo.CommandQueue = native.m_GraphicsQueue;
		initInfo.NumFramesInFlight = native.m_FrameSlotCount;
		initInfo.RTVFormat = native.m_ColorFormat;
		initInfo.DSVFormat = DXGI_FORMAT_UNKNOWN;
		initInfo.SrvDescriptorHeap = native.m_TextureHeap;
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
			m_Interop.reset();
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
		m_Interop.reset();
		m_IsInitialized = false;
	}

	bool DevelopGuiDX12RenderBackend::NewFrame() noexcept
	{
		GGLAB_ASSERT(m_IsInitialized);
		if (!m_IsInitialized)
		{
			return false;
		}
		ImGui_ImplDX12_NewFrame();
		return true;
	}

	void DevelopGuiDX12RenderBackend::RenderDrawData(
		RHIGraphicsCommandContext* commandContext, RHITextureViewHandle renderTarget) noexcept
	{
		GGLAB_ASSERT(m_IsInitialized);
		if (!m_IsInitialized)
		{
			return;
		}

		auto* nativeCommands = m_Interop->PrepareDraw(commandContext, renderTarget);
		if (!nativeCommands)
		{
			return;
		}

		ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), nativeCommands);
	}

	ImTextureID DevelopGuiDX12RenderBackend::ResolveTextureId(
		RHIDescriptorHandle descriptor) const noexcept
	{
		if (!m_IsInitialized || !m_Interop)
		{
			return {};
		}

		return static_cast<ImTextureID>(m_Interop->ResolveTexture(descriptor).ptr);
	}

	void DevelopGuiDX12RenderBackend::DescriptorAlloc(ImGui_ImplDX12_InitInfo* info,
		D3D12_CPU_DESCRIPTOR_HANDLE* outCpuHandle, D3D12_GPU_DESCRIPTOR_HANDLE* outGpuHandle)
	{
		auto* backend = static_cast<DevelopGuiDX12RenderBackend*>(info->UserData);
		auto descriptorView = backend->m_Interop->AllocateTextureDescriptor();
		*outCpuHandle = descriptorView.m_CpuHandle;
		*outGpuHandle = descriptorView.m_GpuHandle;
	}

	void DevelopGuiDX12RenderBackend::DescriptorFree(ImGui_ImplDX12_InitInfo* info,
		D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle, D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle)
	{
		GGLAB_UNUSED(cpuHandle);
		auto* backend = static_cast<DevelopGuiDX12RenderBackend*>(info->UserData);
		backend->m_Interop->RetireTextureDescriptor(gpuHandle);
	}
}
