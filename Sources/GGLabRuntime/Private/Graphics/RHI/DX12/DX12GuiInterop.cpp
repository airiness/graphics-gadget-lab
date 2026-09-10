#include "GGLabRuntime/Graphics/RHI/DX12/DX12GuiInterop.h"
#include "Graphics/RHI/DX12/DX12Context.h"
#include "Graphics/RHI/DX12/DX12Device.h"
#include "Graphics/RHI/DX12/DX12QueueSystem.h"
#include "Graphics/RHI/DX12/DX12CommandQueue.h"
#include "Graphics/RHI/DX12/DX12CommandList.h"
#include "Graphics/RHI/DX12/DX12CommandContext.h"
#include "Graphics/RHI/DX12/Descriptor/DX12DescriptorManager.h"
#include "Graphics/RHI/DX12/Descriptor/DX12DescriptorFreeListAllocator.h"
#include "Graphics/RHI/DX12/Descriptor/DX12DescriptorHeap.h"
#include "Graphics/Utility/DXGIFormatUtils.h"

#include <span>

namespace gglab
{
	namespace
	{
		class DX12GuiInterop final : public DX12GuiInteropBase
		{
		public:
			explicit DX12GuiInterop(DX12Context& context) noexcept : m_Context(context) {}

			DX12GuiNativeInfo GetNativeInfo() const noexcept override
			{
				return { m_Context.GetDX12Device().Get(),
					m_Context.GetQueueSystem().GetQueue(DX12QueueType::Graphics).Get(),
					GetTextureHeap().Get(), ToDXGIFormat(m_Context.GetSwapChain().GetFormat()),
					m_Context.GetFrameSlotCount() };
			}

			DX12GuiDescriptor AllocateTextureDescriptor() noexcept override
			{
				const auto view = m_Context.GetDescriptorManager().AllocateDevelopGuiSrvView();
				return { view.m_CpuHandle, view.m_GpuHandle };
			}

			void RetireTextureDescriptor(D3D12_GPU_DESCRIPTOR_HANDLE handle) noexcept override
			{
				m_Context.GetDescriptorManager().DeferFreeDevelopGuiSrvInFrame(handle);
			}

			D3D12_GPU_DESCRIPTOR_HANDLE ResolveTexture(RHIDescriptorHandle descriptor) const noexcept override
			{
				if (!descriptor.IsValid() || descriptor.m_HeapType != RHIDescriptorHeapType::CbvSrvUav)
					return {};
				auto* heap = m_Context.GetDescriptorManager().GetHeap(DX12DescriptorManager::HeapType::CbvSrvUav);
				if (!heap) return {};
				return heap->GpuHandleAt(descriptor.m_Index);
			}

			ID3D12GraphicsCommandList* PrepareDraw(RHIGraphicsCommandContext* commands,
				RHITextureViewHandle target) noexcept override
			{
				auto* native = dynamic_cast<DX12GraphicsCommandContext*>(commands);
				GGLAB_ASSERT_NOT_NULL(native);
				if (!native) return nullptr;
				const RHIRenderingAttachment attachment{ .m_View = target };
				commands->BeginRendering({ .m_ColorAttachments =
					std::span<const RHIRenderingAttachment>(&attachment, 1) });
				native->GetCommandList()->SetDescriptorHeap(GetTextureHeap());
				return native->Get();
			}

		private:
			DX12DescriptorHeap& GetTextureHeap() const noexcept
			{
				return *m_Context.GetDescriptorManager().GetFreeListAllocator(
					DX12DescriptorManager::AllocatorType::DevelopGuiSrv)->GetHeap();
			}
			DX12Context& m_Context;
		};
	}

	std::unique_ptr<DX12GuiInteropBase> CreateDX12GuiInterop(RHIContext& context) noexcept
	{
		auto* native = dynamic_cast<DX12Context*>(&context);
		return native ? std::make_unique<DX12GuiInterop>(*native) : nullptr;
	}
}
