#pragma once
#include "GGLabRuntime/Graphics/RHI/RHIDescriptor.h"
#include "GGLabRuntime/Graphics/RHI/RHITexture.h"

#include <d3d12.h>
#include <cstdint>
#include <memory>

namespace gglab
{
	class RHIContext;
	class RHIGraphicsCommandContext;

	struct DX12GuiNativeInfo
	{
		ID3D12Device* m_Device = nullptr;
		ID3D12CommandQueue* m_GraphicsQueue = nullptr;
		ID3D12DescriptorHeap* m_TextureHeap = nullptr;
		DXGI_FORMAT m_ColorFormat = DXGI_FORMAT_UNKNOWN;
		uint32_t m_FrameSlotCount = 0;
	};

	struct DX12GuiDescriptor
	{
		D3D12_CPU_DESCRIPTOR_HANDLE m_CpuHandle{};
		D3D12_GPU_DESCRIPTOR_HANDLE m_GpuHandle{};
	};

	// Native GUI integration only. The RHI context outlives this borrowed adapter,
	// native objects and GUI renderer. Finalize GUI at the host's quiescent boundary.
	class DX12GuiInteropBase
	{
	public:
		virtual ~DX12GuiInteropBase() = default;
		[[nodiscard]] virtual DX12GuiNativeInfo GetNativeInfo() const noexcept = 0;
		[[nodiscard]] virtual DX12GuiDescriptor AllocateTextureDescriptor() noexcept = 0;
		// Uses the existing frame-deferred retirement path, never immediate GPU reuse.
		virtual void RetireTextureDescriptor(D3D12_GPU_DESCRIPTOR_HANDLE handle) noexcept = 0;
		[[nodiscard]] virtual D3D12_GPU_DESCRIPTOR_HANDLE ResolveTexture(
			RHIDescriptorHandle descriptor) const noexcept = 0;
		// Begins the RHI attachment and binds the GUI heap through Runtime's state cache.
		[[nodiscard]] virtual ID3D12GraphicsCommandList* PrepareDraw(
			RHIGraphicsCommandContext* commands, RHITextureViewHandle target) noexcept = 0;
	};

	[[nodiscard]] std::unique_ptr<DX12GuiInteropBase> CreateDX12GuiInterop(RHIContext& context) noexcept;
}
