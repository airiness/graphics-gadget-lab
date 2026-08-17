#pragma once
#include "GGLabFoundation/Base/CoreMacros.h"
#include "Graphics/RHI/RHIContext.h"

#include <cstdint>
#include <memory>
#include <vector>

namespace gglab
{
	struct VulkanBootstrapRuntimeResult;
	class VulkanComputeCommandContext;
	class VulkanContext;
	class VulkanContextSwapChain;
	class VulkanDynamicUniformBuffer;
	class VulkanGraphicsCommandContext;
	class VulkanPipelineSystem;
	class VulkanSet0DynamicUniformFrames;

	class VulkanFrameContext final : public RHIFrameContext
	{
	public:
		VulkanFrameContext(VulkanContext* context, uint32_t frameSlotIndex) noexcept;
		~VulkanFrameContext() override = default;

		uint32_t GetFrameSlotIndex() const noexcept override { return m_FrameSlotIndex; }
		uint32_t GetBackBufferIndex() const noexcept override { return m_BackBufferIndex; }
		RHITextureHandle GetBackBuffer() const noexcept override;
		RHIGraphicsCommandContext& GetGraphicsContext() noexcept override;
		RHIComputeCommandContext& GetDirectComputeContext() noexcept override;
		RHIComputeCommandContext* GetComputeContext() noexcept override { return nullptr; }
		RHIFencePoint GetSubmittedFence() const noexcept override { return m_SubmittedFence; }

	private:
		friend class VulkanContext;

		VulkanContext* m_Context = nullptr;
		uint32_t m_FrameSlotIndex = 0;
		uint32_t m_BackBufferIndex = 0;
		RHIFencePoint m_SubmittedFence{};
		bool m_Active = false;
	};

	class VulkanContext final : public RHIContext
	{
	public:
		[[nodiscard]] static std::unique_ptr<VulkanContext> Create(
			const RHIContextDesc& desc) noexcept;
		GGLAB_DELETE_COPYABLE_MOVABLE(VulkanContext);
		~VulkanContext() override;

		RHIDevice& GetDevice() noexcept override;
		const RHIDevice& GetDevice() const noexcept override;
		RHISwapChain& GetSwapChain() noexcept override;
		const RHISwapChain& GetSwapChain() const noexcept override;
		TransferManager& GetTransferManager() noexcept override;
		RHIPipelineSystem& GetPipelineSystem() noexcept override;
		GpuProfiler* GetGpuProfiler() noexcept override { return nullptr; }

		RHIFrameContext& BeginFrame() noexcept override;
		RHIFencePoint EndFrame(RHIFrameContext& frame) noexcept override;
		void AbortFrame(RHIFrameContext& frame) noexcept override;
		void WaitForFence(
			RHIQueueType waitingQueue, const RHIFencePoint& fencePoint) noexcept override;

		void Resize(uint32_t width, uint32_t height) noexcept override;
		void WaitIdle() noexcept override;
		void RetireCompletedWork() noexcept override;
		uint32_t GetFrameSlotCount() const noexcept override;

	private:
		friend class VulkanContextSwapChain;
		friend class VulkanFrameContext;

		VulkanContext() noexcept = default;
		[[nodiscard]] bool Initialize(const RHIContextDesc& desc) noexcept;
		[[nodiscard]] bool RecreateSwapChain(uint32_t width, uint32_t height) noexcept;
		void FinishFrame(VulkanFrameContext& frame, const RHIFencePoint& fencePoint) noexcept;
		void Finalize() noexcept;

		// Bootstrap owns instance/surface/device/frame-runtime and therefore
		// stays alive until every composed RHI service has been destroyed.
		std::unique_ptr<VulkanBootstrapRuntimeResult> m_Bootstrap;
		std::unique_ptr<VulkanPipelineSystem> m_PipelineSystem;
		std::unique_ptr<TransferManager> m_TransferManager;
		std::unique_ptr<VulkanDynamicUniformBuffer> m_DynamicUniformBuffer;
		std::unique_ptr<VulkanSet0DynamicUniformFrames> m_Set0Frames;
		std::unique_ptr<VulkanGraphicsCommandContext> m_GraphicsContext;
		std::unique_ptr<VulkanComputeCommandContext> m_DirectComputeContext;
		std::unique_ptr<VulkanContextSwapChain> m_SwapChain;
		std::vector<std::unique_ptr<VulkanFrameContext>> m_Frames;
		VulkanFrameContext* m_ActiveFrame = nullptr;
		uint32_t m_Width = 0;
		uint32_t m_Height = 0;
		bool m_Vsync = false;
		bool m_RecreatePending = false;
		bool m_CompletedProductionFrame = false;
		bool m_Initialized = false;
	};
}
