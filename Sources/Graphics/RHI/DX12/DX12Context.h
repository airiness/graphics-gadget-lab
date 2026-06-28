#pragma once
#include "Graphics/RHI/RHIContext.h"

#include <memory>
#include <vector>

namespace gglab
{
	class DX12CommandAllocator;
	class DX12ComputeCommandContext;
	class DX12DescriptorManager;
	class DX12Device;
	class DX12GraphicsCommandContext;
	class DX12QueueSystem;

	class DX12Context;

	class DX12FrameContext final : public RHIFrameContext
	{
	public:
		DX12FrameContext(DX12Context* context, uint32_t frameIndex) noexcept;
		~DX12FrameContext() override = default;

		uint32_t GetBackBufferIndex() const noexcept override { return m_BackBufferIndex; }
		RHITextureHandle GetBackBuffer() const noexcept override;
		RHIGraphicsCommandContext& GetGraphicsContext() noexcept override;
		RHIComputeCommandContext* GetComputeContext() noexcept override;
		RHIFencePoint GetSubmittedFence() const noexcept override { return m_SubmittedFence; }

	private:
		friend class DX12Context;

		DX12Context* m_Context = nullptr;
		uint32_t m_FrameIndex = 0;
		uint32_t m_BackBufferIndex = 0;
		DX12CommandAllocator* m_GraphicsAllocator = nullptr;
		DX12CommandAllocator* m_ComputeAllocator = nullptr;
		RHIFencePoint m_SubmittedFence{};
		bool m_Active = false;
	};

	class DX12Context final : public RHIContext
	{
	public:
		explicit DX12Context(const RHIContextDesc& desc) noexcept;
		GGLAB_DELETE_COPYABLE_MOVABLE(DX12Context);
		~DX12Context() override;

		RHIDevice& GetDevice() noexcept override;
		const RHIDevice& GetDevice() const noexcept override;
		RHISwapChain& GetSwapChain() noexcept override;
		const RHISwapChain& GetSwapChain() const noexcept override;
		TransferManager& GetTransferManager() noexcept override;

		RHIFrameContext& BeginFrame() noexcept override;
		RHIFencePoint EndFrame(RHIFrameContext& frame) noexcept override;
		void AbortFrame(RHIFrameContext& frame) noexcept override;
		void WaitForFence(RHIQueueType waitingQueue,
			const RHIFencePoint& fencePoint) noexcept override;

		void Resize(uint32_t width, uint32_t height) noexcept override;
		void WaitIdle() noexcept override;
		void RetireCompletedWork() noexcept override;
		uint32_t GetCurrentFrameIndex() const noexcept override;

		[[nodiscard]] DX12Device& GetDX12Device() noexcept;
		[[nodiscard]] const DX12Device& GetDX12Device() const noexcept;
		[[nodiscard]] DX12DescriptorManager& GetDescriptorManager() noexcept;
		[[nodiscard]] DX12QueueSystem& GetQueueSystem() noexcept;
		[[nodiscard]] const DX12QueueSystem& GetQueueSystem() const noexcept;

	private:
		friend class DX12FrameContext;

		DX12ComputeCommandContext* AcquireComputeContext(DX12FrameContext& frame) noexcept;
		void BeginGraphicsRecording(DX12FrameContext& frame) noexcept;
		void BindGlobalDescriptorHeaps(class DX12CommandList& commandList) noexcept;
		void FinishFrame(DX12FrameContext& frame, const RHIFencePoint& fencePoint) noexcept;
		void Finalize() noexcept;

		std::unique_ptr<DX12Device> m_Device;
		std::unique_ptr<DX12QueueSystem> m_QueueSystem;
		std::unique_ptr<RHISwapChain> m_SwapChain;
		std::unique_ptr<DX12DescriptorManager> m_DescriptorManager;
		std::unique_ptr<TransferManager> m_TransferManager;
		std::vector<std::unique_ptr<DX12FrameContext>> m_Frames;
		DX12FrameContext* m_ActiveFrame = nullptr;
		bool m_Initialized = false;
	};
}
