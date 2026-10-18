#pragma once
#include "GGLabFoundation/Base/CoreMacros.h"
#include "GGLabRuntime/Graphics/RHI/RHIContext.h"
#include "GGLabRuntime/Graphics/RHI/RHIUpload.h"
#include "Graphics/RHI/RHIFrameSubmission.h"
#include "Graphics/RHI/RHIUploadPool.h"

#include <cstdint>
#include <memory>
#include <string_view>
#include <vector>

namespace gglab
{
	struct VulkanBootstrapRuntimeResult;
	class VulkanSurfaceFactoryBase;
	class VulkanComputeCommandContext;
	class VulkanContext;
	class VulkanContextSwapChain;
	class VulkanGraphicsCommandContext;
	class VulkanGpuProfiler;
	class VulkanPipelineSystem;
	class VulkanSet0DynamicUniformFrames;
	class VulkanDevice;
	class VulkanSwapChain;
	struct VulkanBackendSnapshot;

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
		RHIFrameUploads GetUploads() noexcept override;

	private:
		friend class VulkanContext;

		VulkanContext* m_Context = nullptr;
		uint32_t m_FrameSlotIndex = 0;
		uint32_t m_BackBufferIndex = 0;
		RHIUploadWriter m_UploadWriter;
		std::unique_ptr<RHIUploadBatch> m_Uploads;
		bool m_Active = false;
	};

	class VulkanContext final : public RHIContext
	{
	public:
		[[nodiscard]] static std::unique_ptr<VulkanContext> Create(
			const RHIContextDesc& desc, const VulkanSurfaceFactoryBase& surfaceFactory,
			bool isHostAbiSupported) noexcept;
		GGLAB_DELETE_COPYABLE_MOVABLE(VulkanContext);
		~VulkanContext() override;

		RHIDevice& GetDevice() noexcept override;
		const RHIDevice& GetDevice() const noexcept override;
		RHISwapChain& GetSwapChain() noexcept override;
		const RHISwapChain& GetSwapChain() const noexcept override;
		TransferManager& GetTransferManager() noexcept override;
		RHIPipelineSystem& GetPipelineSystem() noexcept override;
		GpuProfiler* GetGpuProfiler() noexcept override;

		RHIFrameBeginResult BeginFrame() noexcept override;
		RHIFrameEndResult EndFrame(RHIFrameContext& frame) noexcept override;
		RHIFrameEndResult AbortFrame(RHIFrameContext& frame) noexcept override;
		bool WaitForFence(
			RHIQueueType waitingQueue, const RHIFencePoint& fencePoint) noexcept override;

		void Resize(uint32_t width, uint32_t height) noexcept override;
		RHIGpuWaitResult WaitIdleFor(uint32_t timeoutMilliseconds) noexcept override;
		RHIUploadStatistics GetUploadStatistics() const noexcept override
		{
			return m_UploadPool ? m_UploadPool->GetStatistics() : RHIUploadStatistics{};
		}
		void RetireCompletedWork() noexcept override;
		bool IsPoisoned() const noexcept override { return m_Poisoned; }
		uint32_t GetFrameSlotCount() const noexcept override;

		// Backend-specific integration surface. These borrowed views keep
		// presentation resources owned by Vulkan while allowing Vulkan-only
		// adapters to bind to the native contract.
		[[nodiscard]] VulkanDevice& GetVulkanDevice() noexcept;
		[[nodiscard]] const VulkanDevice& GetVulkanDevice() const noexcept;
		[[nodiscard]] const VulkanSwapChain& GetVulkanSwapChain() const noexcept;
		[[nodiscard]] uint64_t GetSwapChainGeneration() const noexcept
		{
			return m_SwapChainGeneration;
		}
		[[nodiscard]] bool IsValidationRequested() const noexcept
		{
			return m_ValidationRequested;
		}
		[[nodiscard]] bool IsDeviceLost() const noexcept override;
		[[nodiscard]] uint64_t GetSubmittedTimelineValue() const noexcept;
		[[nodiscard]] bool TryGetCompletedTimelineValue(uint64_t& outValue) const noexcept;

	private:
		friend class VulkanContextSwapChain;
		friend class VulkanFrameContext;
		friend void BuildVulkanBackendSnapshot(
			const VulkanContext& context, VulkanBackendSnapshot& outSnapshot) noexcept;

		VulkanContext() noexcept = default;
		[[nodiscard]] bool Initialize(const RHIContextDesc& desc,
			const VulkanSurfaceFactoryBase& surfaceFactory, bool isHostAbiSupported) noexcept;
		[[nodiscard]] bool RecreateSwapChain(uint32_t width, uint32_t height) noexcept;
		void FinishFrame(VulkanFrameContext& frame) noexcept;
		[[nodiscard]] RHISubmissionVerification MakeSubmissionVerification() noexcept;
		// Records natively confirmed device loss in the runtime health state.
		void ObserveWaitResult(RHIGpuWaitResult result, std::string_view operation) noexcept;
		void Finalize() noexcept;
		// Leaks every GPU-visible owner when teardown cannot verify idle on a live device.
		void AbandonGpuOwners() noexcept;

		// Bootstrap owns instance/surface/device/frame-runtime and therefore
		// stays alive until every composed RHI service has been destroyed.
		std::unique_ptr<VulkanBootstrapRuntimeResult> m_Bootstrap;
		std::unique_ptr<VulkanPipelineSystem> m_PipelineSystem;
		std::unique_ptr<TransferManager> m_TransferManager;
		std::unique_ptr<RHIUploadPool> m_UploadPool;
		std::unique_ptr<VulkanSet0DynamicUniformFrames> m_Set0Frames;
		std::unique_ptr<VulkanGpuProfiler> m_GpuProfiler;
		std::unique_ptr<VulkanGraphicsCommandContext> m_GraphicsContext;
		std::unique_ptr<VulkanComputeCommandContext> m_DirectComputeContext;
		std::unique_ptr<VulkanContextSwapChain> m_SwapChain;
		std::vector<std::unique_ptr<VulkanFrameContext>> m_Frames;
		VulkanFrameContext* m_ActiveFrame = nullptr;
		uint32_t m_Width = 0;
		uint32_t m_Height = 0;
		uint64_t m_SwapChainGeneration = 0;
		bool m_Vsync = false;
		bool m_ValidationRequested = false;
		bool m_RecreatePending = false;
		bool m_CompletedProductionFrame = false;
		bool m_Initialized = false;
		bool m_Poisoned = false;
	};
}
