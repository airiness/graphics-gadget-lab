#include "Graphics/RHI/Vulkan/VulkanContext.h"
#include "GGLabRuntime/Core/Log/LogMacros.h"
#include "Graphics/RHI/Vulkan/VulkanBootstrap.h"
#include "Graphics/RHI/Vulkan/VulkanCommandContext.h"
#include "Graphics/RHI/Vulkan/VulkanDynamicUniformBuffer.h"
#include "Graphics/RHI/Vulkan/VulkanGpuProfiler.h"
#include "Graphics/RHI/Vulkan/VulkanPipelineSystem.h"
#include "Graphics/RHI/Vulkan/VulkanResourceManager.h"
#include "Graphics/RHI/Vulkan/VulkanTransferContext.h"
#include "Graphics/TransferManager.h"

#include <format>
#include <string>
#include <utility>

namespace gglab
{
	class VulkanContextSwapChain final : public RHISwapChain
	{
	public:
		explicit VulkanContextSwapChain(VulkanContext* context) noexcept : m_Context(context)
		{
			GGLAB_ASSERT_NOT_NULL(m_Context);
		}
		~VulkanContextSwapChain() override { Finalize(); }

		[[nodiscard]] bool ImportBackBuffers() noexcept
		{
			if (!m_Context || !m_Context->m_Bootstrap || !m_BackBuffers.empty())
			{
				return false;
			}
			auto& runtime = *m_Context->m_Bootstrap->m_FrameRuntime;
			auto& nativeSwapChain = runtime.GetSwapChain();
			auto& resources = m_Context->m_Bootstrap->m_Device->GetResourceManager();
			m_BackBuffers.resize(nativeSwapChain.GetImageCount());
			for (uint32_t index = 0; index < nativeSwapChain.GetImageCount(); ++index)
			{
				VulkanResourceManager::ImportedTextureDesc importDesc{};
				importDesc.m_RHI.m_Desc = {
					.m_Dimension = RHITextureDimension::Texture2D,
					.m_Format = nativeSwapChain.GetFormat(),
					.m_Usage = RHITextureUsage::RenderTarget | RHITextureUsage::Present,
					.m_Extent = { nativeSwapChain.GetWidth(), nativeSwapChain.GetHeight(), 1 },
					.m_ArraySize = 1,
					.m_MipLevels = 1,
					.m_SampleCount = 1,
					.m_DebugName = "VulkanSwapChain.BackBuffer",
				};
				importDesc.m_RHI.m_External.m_InitialState = UndefinedRHITextureState();
				importDesc.m_Image = nativeSwapChain.GetImage(index).m_Image;
				importDesc.m_DebugIdentity = {
					.m_Domain = RHIResourceDebugDomain::SwapChain,
					.m_Category = "BackBuffer",
					.m_Label = "PresentationSurface",
					.m_StableId = index,
				};
				m_BackBuffers[index] = resources.ImportTexture(importDesc);
				if (!m_BackBuffers[index].IsValid())
				{
					GGLAB_LOG_GRAPHICS_ERROR_ALWAYS(std::format(
						"Failed to import Vulkan swapchain image {} into the RHI.", index));
					ReleaseBackBuffers();
					return false;
				}
			}
			m_Valid = true;
			return true;
		}

		void ReleaseBackBuffers() noexcept
		{
			if (m_Context && m_Context->m_Bootstrap && m_Context->m_Bootstrap->m_Device)
			{
				auto& device = *m_Context->m_Bootstrap->m_Device;
				for (RHITextureHandle& backBuffer : m_BackBuffers)
				{
					if (backBuffer.IsValid())
					{
						device.DestroyTexture(backBuffer);
						backBuffer.Reset();
					}
				}
			}
			m_BackBuffers.clear();
			m_Valid = false;
		}

		void Finalize() noexcept { ReleaseBackBuffers(); }
		bool IsValid() const noexcept override { return m_Valid && !m_BackBuffers.empty(); }
		uint32_t GetBufferCount() const noexcept override
		{
			return static_cast<uint32_t>(m_BackBuffers.size());
		}
		uint32_t GetBufferWidth() const noexcept override
		{
			return IsValid()
				? m_Context->m_Bootstrap->m_FrameRuntime->GetSwapChain().GetWidth()
				: 0;
		}
		uint32_t GetBufferHeight() const noexcept override
		{
			return IsValid()
				? m_Context->m_Bootstrap->m_FrameRuntime->GetSwapChain().GetHeight()
				: 0;
		}
		RHIFormat GetFormat() const noexcept override
		{
			return IsValid()
				? m_Context->m_Bootstrap->m_FrameRuntime->GetSwapChain().GetFormat()
				: RHIFormat::Unknown;
		}
		RHITextureHandle GetBackBufferHandle(uint32_t bufferIndex) const noexcept override
		{
			return bufferIndex < m_BackBuffers.size() ? m_BackBuffers[bufferIndex]
				: RHITextureHandle{};
		}
		RHIResourceState GetBackBufferInitialState(uint32_t bufferIndex) const noexcept override
		{
			if (!IsValid() || bufferIndex >= m_BackBuffers.size())
			{
				return UndefinedRHITextureState();
			}
			return ToRHIBackBufferInitialState(
				m_Context->m_Bootstrap->m_FrameRuntime->GetLayoutTracker().Get(bufferIndex));
		}
	private:
		VulkanContext* m_Context = nullptr;
		std::vector<RHITextureHandle> m_BackBuffers;
		bool m_Valid = false;
	};

	VulkanFrameContext::VulkanFrameContext(
		VulkanContext* context, uint32_t frameSlotIndex) noexcept :
		m_Context(context), m_FrameSlotIndex(frameSlotIndex)
	{
		GGLAB_ASSERT_NOT_NULL(m_Context);
	}

	RHITextureHandle VulkanFrameContext::GetBackBuffer() const noexcept
	{
		return m_Context->GetSwapChain().GetBackBufferHandle(m_BackBufferIndex);
	}

	RHIGraphicsCommandContext& VulkanFrameContext::GetGraphicsContext() noexcept
	{
		GGLAB_ASSERT_MSG(m_Active,
			"VulkanFrameContext::GetGraphicsContext requires an active frame.");
		return *m_Context->m_GraphicsContext;
	}

	RHIComputeCommandContext& VulkanFrameContext::GetDirectComputeContext() noexcept
	{
		GGLAB_ASSERT_MSG(m_Active,
			"VulkanFrameContext::GetDirectComputeContext requires an active frame.");
		return *m_Context->m_DirectComputeContext;
	}

	std::unique_ptr<VulkanContext> VulkanContext::Create(const RHIContextDesc& desc,
		const VulkanSurfaceFactoryBase& surfaceFactory, bool isHostAbiSupported) noexcept
	{
		auto context = std::unique_ptr<VulkanContext>(new VulkanContext());
		if (!context->Initialize(desc, surfaceFactory, isHostAbiSupported))
		{
			return {};
		}
		return context;
	}

	VulkanContext::~VulkanContext()
	{
		Finalize();
	}

	bool VulkanContext::Initialize(const RHIContextDesc& desc,
		const VulkanSurfaceFactoryBase& surfaceFactory, bool isHostAbiSupported) noexcept
	{
		if (desc.m_Width == 0 || desc.m_Height == 0 || desc.m_FrameSlotCount == 0)
		{
			GGLAB_LOG_GRAPHICS_ERROR_ALWAYS(
				"VulkanContext requires a nonzero extent and at least one frame slot.");
			return false;
		}

		VulkanBootstrapRuntimeCreateInfo createInfo{};
		createInfo.m_BootstrapOptions.m_SurfaceFactory = &surfaceFactory;
		createInfo.m_BootstrapOptions.m_IsHostAbiSupported = isHostAbiSupported;
		createInfo.m_BootstrapOptions.m_RequestValidation = desc.m_EnableDebugValidation;
		createInfo.m_BootstrapOptions.m_SelectionRequest =
			ParseVulkanAdapterSelectionRequest(desc.m_AdapterSelector);
		createInfo.m_FrameSlotCount = desc.m_FrameSlotCount;
		createInfo.m_RequestedFormat = desc.m_BackBufferFormat;
		createInfo.m_Vsync = desc.m_Vsync;
		createInfo.m_Width = desc.m_Width;
		createInfo.m_Height = desc.m_Height;
		VulkanBootstrapRuntimeResult bootstrap = CreateVulkanBootstrapRuntime(createInfo);
		if (!bootstrap.Succeeded())
		{
			GGLAB_LOG_GRAPHICS_ERROR_ALWAYS(std::format(
				"Vulkan RHI initialization failed: {} (VkResult {}).",
				bootstrap.m_Error.empty() ? "bootstrap did not create a frame runtime"
				: bootstrap.m_Error,
				static_cast<int32_t>(bootstrap.m_Result)));
			return false;
		}
		m_Bootstrap = std::make_unique<VulkanBootstrapRuntimeResult>(std::move(bootstrap));
		auto& device = *m_Bootstrap->m_Device;
		auto& runtime = *m_Bootstrap->m_FrameRuntime;

		m_PipelineSystem = std::make_unique<VulkanPipelineSystem>(&device);
		m_TransferManager = std::make_unique<TransferManager>(
			std::make_unique<VulkanTransferContext>(&device));
		m_DynamicUniformBuffer = std::make_unique<VulkanDynamicUniformBuffer>();
		if (!m_DynamicUniformBuffer->Initialize(&device, runtime.GetFrameSlotCount()))
		{
			GGLAB_LOG_GRAPHICS_ERROR_ALWAYS(
				"VulkanContext failed to initialize its dynamic uniform arena.");
			Finalize();
			return false;
		}
		m_Set0Frames = std::make_unique<VulkanSet0DynamicUniformFrames>();
		if (!m_Set0Frames->Initialize(
			&device, m_DynamicUniformBuffer.get(), runtime.GetFrameSlotCount()))
		{
			GGLAB_LOG_GRAPHICS_ERROR_ALWAYS(
				"VulkanContext failed to initialize its frame-local set-0 arenas.");
			Finalize();
			return false;
		}
		m_GpuProfiler =
			std::make_unique<VulkanGpuProfiler>(&device, runtime.GetFrameSlotCount());
		m_GraphicsContext = std::make_unique<VulkanGraphicsCommandContext>(&device,
			m_PipelineSystem.get(), m_DynamicUniformBuffer.get(), m_Set0Frames.get(),
			m_GpuProfiler.get());
		m_DirectComputeContext =
			std::make_unique<VulkanComputeCommandContext>(*m_GraphicsContext);

		m_SwapChain = std::make_unique<VulkanContextSwapChain>(this);
		if (!m_SwapChain->ImportBackBuffers())
		{
			Finalize();
			return false;
		}
		m_Frames.reserve(runtime.GetFrameSlotCount());
		for (uint32_t frameSlotIndex = 0; frameSlotIndex < runtime.GetFrameSlotCount();
			++frameSlotIndex)
		{
			m_Frames.push_back(
				std::make_unique<VulkanFrameContext>(this, frameSlotIndex));
		}
		m_Width = desc.m_Width;
		m_Height = desc.m_Height;
		m_Vsync = desc.m_Vsync;
		m_ValidationRequested = desc.m_EnableDebugValidation;
		m_SwapChainGeneration = 1;
		m_Initialized = true;
		GGLAB_LOG_GRAPHICS_INFO_ALWAYS(std::format(
			"Vulkan RHI context initialized on '{}'.",
			m_Bootstrap->m_SelectedSnapshot.m_Identity.m_DeviceName));
		return true;
	}

	RHIDevice& VulkanContext::GetDevice() noexcept
	{
		return *m_Bootstrap->m_Device;
	}

	const RHIDevice& VulkanContext::GetDevice() const noexcept
	{
		return *m_Bootstrap->m_Device;
	}

	RHISwapChain& VulkanContext::GetSwapChain() noexcept
	{
		return *m_SwapChain;
	}

	const RHISwapChain& VulkanContext::GetSwapChain() const noexcept
	{
		return *m_SwapChain;
	}

	TransferManager& VulkanContext::GetTransferManager() noexcept
	{
		return *m_TransferManager;
	}

	RHIPipelineSystem& VulkanContext::GetPipelineSystem() noexcept
	{
		return *m_PipelineSystem;
	}

	GpuProfiler* VulkanContext::GetGpuProfiler() noexcept
	{
		return m_GpuProfiler.get();
	}

	RHIFrameBeginResult VulkanContext::BeginFrame() noexcept
	{
		GGLAB_ASSERT_MSG(m_Initialized, "VulkanContext::BeginFrame called after finalization.");
		GGLAB_ASSERT_MSG(m_ActiveFrame == nullptr,
			"VulkanContext only supports one active frame transaction.");
		if (!m_Initialized || m_ActiveFrame != nullptr)
		{
			return RHIFrameBeginResult::Fatal();
		}

		if (m_RecreatePending && !RecreateSwapChain(m_Width, m_Height))
		{
			GGLAB_LOG_GRAPHICS_ERROR_ALWAYS(
				"VulkanContext failed to recreate its pending swapchain.");
			return RHIFrameBeginResult::Fatal();
		}

		VulkanBeginFrameResult begin{};
		bool recreateFailed = false;
		for (uint32_t attempt = 0; attempt < 2; ++attempt)
		{
			begin = m_Bootstrap->m_FrameRuntime->BeginFrame();
			if (begin.m_Status != VulkanAcquireOutcome::OutOfDate)
			{
				break;
			}
			if (!RecreateSwapChain(m_Width, m_Height))
			{
				recreateFailed = true;
				break;
			}
		}
		if (!begin.IsAcquired())
		{
			if (!recreateFailed && begin.m_Status == VulkanAcquireOutcome::OutOfDate)
			{
				// The final OUT_OF_DATE attempt already completed a successful
				// recreation. Leave the new swapchain ready for acquisition on the
				// next tick instead of scheduling another recreation first.
				GGLAB_ASSERT(!m_RecreatePending);
				return RHIFrameBeginResult::Unavailable();
			}
			GGLAB_LOG_GRAPHICS_ERROR_ALWAYS(std::format(
				"VulkanContext failed to acquire a frame (VkResult {}).",
				static_cast<int32_t>(begin.m_Result)));
			return RHIFrameBeginResult::Fatal();
		}

		RetireCompletedWork();
		if (!m_Set0Frames->BeginFrame(begin.m_FrameSlotIndex))
		{
			GGLAB_UNUSED(m_Bootstrap->m_FrameRuntime->AbortFrame());
			GGLAB_LOG_GRAPHICS_ERROR_ALWAYS(
				"VulkanContext failed to begin its frame-local descriptor arena.");
			return RHIFrameBeginResult::Fatal();
		}
		const VulkanFrameRecording recording =
			m_Bootstrap->m_FrameRuntime->BeginFrameRecording(
				VulkanPresentTransitionOwnership::CommandStream);
		if (!recording.IsValid() || !m_GraphicsContext->BeginEncoding(
			recording.m_CommandBuffer, begin.m_FrameSlotIndex))
		{
			m_GraphicsContext->AbortEncoding();
			GGLAB_UNUSED(m_Set0Frames->AbortFrame(begin.m_FrameSlotIndex));
			GGLAB_UNUSED(m_Bootstrap->m_FrameRuntime->AbortFrame());
			GGLAB_LOG_GRAPHICS_ERROR_ALWAYS(
				"VulkanContext failed to begin command encoding.");
			return RHIFrameBeginResult::Fatal();
		}

		auto& frame = *m_Frames[begin.m_FrameSlotIndex];
		frame.m_BackBufferIndex = begin.m_BackBufferIndex;
		frame.m_Active = true;
		m_ActiveFrame = &frame;
		m_RecreatePending = begin.m_RecreatePending;
		return RHIFrameBeginResult::Ready(frame);
	}

	RHIFrameEndResult VulkanContext::EndFrame(RHIFrameContext& rhiFrame) noexcept
	{
		auto* frame = dynamic_cast<VulkanFrameContext*>(&rhiFrame);
		GGLAB_ASSERT_MSG(frame && frame == m_ActiveFrame && frame->m_Active,
			"VulkanContext::EndFrame received a foreign or inactive frame.");
		if (!frame || frame != m_ActiveFrame || !frame->m_Active)
		{
			return RHIFrameEndResult::Fatal();
		}

		const std::vector usedTextures(
			m_GraphicsContext->GetUsedTextures().begin(), m_GraphicsContext->GetUsedTextures().end());
		const std::vector usedBuffers(
			m_GraphicsContext->GetUsedBuffers().begin(), m_GraphicsContext->GetUsedBuffers().end());
		if (!m_GraphicsContext->FinishEncoding() ||
			!m_Bootstrap->m_FrameRuntime->EndFrameRecording())
		{
			GGLAB_LOG_GRAPHICS_ERROR_ALWAYS(
				"VulkanContext aborted a frame whose command stream did not finalize.");
			return RHIFrameEndResult::Fatal(AbortFrame(rhiFrame));
		}

		const VulkanSubmitPresentResult result = m_Bootstrap->m_FrameRuntime->EndFrame();
		if (!result.m_Submitted || !result.m_SubmittedFencePoint.IsValid())
		{
			GGLAB_UNUSED(m_Set0Frames->AbortFrame(frame->m_FrameSlotIndex));
			FinishFrame(*frame);
			return RHIFrameEndResult::Fatal();
		}
		const bool descriptorFrameClosed =
			m_Set0Frames->EndFrame(frame->m_FrameSlotIndex, result.m_SubmittedFencePoint);
		if (!descriptorFrameClosed)
		{
			GGLAB_LOG_GRAPHICS_ERROR_ALWAYS(
				"VulkanContext failed to close its submitted frame-local descriptor arena.");
		}
		for (const RHITextureHandle texture : usedTextures)
		{
			m_Bootstrap->m_Device->RecordTextureUse(texture, result.m_SubmittedFencePoint);
		}
		for (const RHIBufferHandle buffer : usedBuffers)
		{
			m_Bootstrap->m_Device->RecordBufferUse(buffer, result.m_SubmittedFencePoint);
		}
		m_RecreatePending = m_RecreatePending || result.m_RecreatePending;
		if (result.IsComplete() && !m_CompletedProductionFrame)
		{
			GGLAB_LOG_GRAPHICS_INFO_ALWAYS(
				"Vulkan completed its first production submit/present frame transaction.");
			m_CompletedProductionFrame = true;
		}
		FinishFrame(*frame);
		return result.IsComplete() && descriptorFrameClosed
			? RHIFrameEndResult::Completed(result.m_SubmittedFencePoint)
			: RHIFrameEndResult::Fatal(result.m_SubmittedFencePoint);
	}

	RHIFencePoint VulkanContext::AbortFrame(RHIFrameContext& rhiFrame) noexcept
	{
		auto* frame = dynamic_cast<VulkanFrameContext*>(&rhiFrame);
		GGLAB_ASSERT_MSG(frame && frame == m_ActiveFrame && frame->m_Active,
			"VulkanContext::AbortFrame received a foreign or inactive frame.");
		if (!frame || frame != m_ActiveFrame || !frame->m_Active)
		{
			return {};
		}

		m_GraphicsContext->AbortEncoding();
		GGLAB_UNUSED(m_Set0Frames->AbortFrame(frame->m_FrameSlotIndex));
		const VulkanSubmitPresentResult result = m_Bootstrap->m_FrameRuntime->AbortFrame();
		m_RecreatePending = m_RecreatePending || result.m_RecreatePending;
		FinishFrame(*frame);
		return result.m_SubmittedFencePoint;
	}

	void VulkanContext::WaitForFence(
		RHIQueueType waitingQueue, const RHIFencePoint& fencePoint) noexcept
	{
		if (!fencePoint.IsValid())
		{
			return;
		}
		if (waitingQueue != RHIQueueType::Graphics || !m_Bootstrap ||
			!m_Bootstrap->m_FrameRuntime ||
			!m_Bootstrap->m_FrameRuntime->QueueGraphicsWait(fencePoint))
		{
			GGLAB_LOG_GRAPHICS_WARN(
				"VulkanContext::WaitForFence rejected an unsupported queue, fence, or frame state.");
		}
	}

	void VulkanContext::Resize(uint32_t width, uint32_t height) noexcept
	{
		GGLAB_ASSERT_MSG(m_ActiveFrame == nullptr,
			"VulkanContext::Resize cannot run during an active frame.");
		if (!m_Initialized || width == 0 || height == 0)
		{
			return;
		}
		GGLAB_UNUSED(RecreateSwapChain(width, height));
	}

	void VulkanContext::WaitIdle() noexcept
	{
		if (!m_Bootstrap || !m_Bootstrap->m_Device)
		{
			return;
		}
		const VkResult result = vkDeviceWaitIdle(m_Bootstrap->m_Device->Get());
		if (result != VK_SUCCESS)
		{
			GGLAB_LOG_GRAPHICS_ERROR_ALWAYS(std::format(
				"VulkanContext::WaitIdle failed with VkResult {}.", static_cast<int32_t>(result)));
		}
	}

	void VulkanContext::RetireCompletedWork() noexcept
	{
		if (m_TransferManager)
		{
			m_TransferManager->Reclaim();
		}
		if (m_Bootstrap && m_Bootstrap->m_Device)
		{
			m_Bootstrap->m_Device->RetireCompletedWork();
		}
	}

	uint32_t VulkanContext::GetFrameSlotCount() const noexcept
	{
		return static_cast<uint32_t>(m_Frames.size());
	}

	VulkanDevice& VulkanContext::GetVulkanDevice() noexcept
	{
		return *m_Bootstrap->m_Device;
	}

	const VulkanDevice& VulkanContext::GetVulkanDevice() const noexcept
	{
		return *m_Bootstrap->m_Device;
	}

	const VulkanSwapChain& VulkanContext::GetVulkanSwapChain() const noexcept
	{
		return m_Bootstrap->m_FrameRuntime->GetSwapChain();
	}

	const VulkanAdapterCapabilitySnapshot&
		VulkanContext::GetAdapterCapabilitySnapshot() const noexcept
	{
		return m_Bootstrap->m_SelectedSnapshot;
	}

	bool VulkanContext::IsValidationEnabled() const noexcept
	{
		return m_Bootstrap && m_Bootstrap->m_HasDebugMessenger;
	}

	bool VulkanContext::IsFrameRuntimeFatal() const noexcept
	{
		return m_Bootstrap && m_Bootstrap->m_FrameRuntime &&
			m_Bootstrap->m_FrameRuntime->IsFatal();
	}

	bool VulkanContext::IsDeviceLost() const noexcept
	{
		return m_Bootstrap && m_Bootstrap->m_FrameRuntime &&
			m_Bootstrap->m_FrameRuntime->IsDeviceLost();
	}

	uint64_t VulkanContext::GetSubmittedTimelineValue() const noexcept
	{
		return m_Bootstrap && m_Bootstrap->m_FrameRuntime
			? m_Bootstrap->m_FrameRuntime->GetTimelineSignalValue()
			: 0;
	}

	bool VulkanContext::TryGetCompletedTimelineValue(uint64_t& outValue) const noexcept
	{
		return m_Bootstrap && m_Bootstrap->m_FrameRuntime &&
			m_Bootstrap->m_FrameRuntime->GetTimeline().GetCompletedValue(outValue) == VK_SUCCESS;
	}

	bool VulkanContext::RecreateSwapChain(uint32_t width, uint32_t height) noexcept
	{
		if (!m_Bootstrap || !m_Bootstrap->m_FrameRuntime || !m_SwapChain ||
			m_ActiveFrame != nullptr || width == 0 || height == 0)
		{
			return false;
		}
		auto& runtime = *m_Bootstrap->m_FrameRuntime;
		std::string error;
		if (!runtime.PrepareSwapChainRecreation(error))
		{
			GGLAB_LOG_GRAPHICS_ERROR_ALWAYS(std::format(
				"Vulkan swapchain recreation preparation failed: {}", error));
			return false;
		}
		m_SwapChain->ReleaseBackBuffers();
		m_Bootstrap->m_Device->RetireCompletedWork();
		if (!runtime.RecreateSwapChainAtGraphicsIdle(width, height, m_Vsync, error))
		{
			GGLAB_LOG_GRAPHICS_ERROR_ALWAYS(std::format(
				"Vulkan swapchain recreation failed: {}", error));
			return false;
		}
		if (!m_SwapChain->ImportBackBuffers())
		{
			return false;
		}
		m_Width = width;
		m_Height = height;
		++m_SwapChainGeneration;
		if (m_SwapChainGeneration == 0)
		{
			m_SwapChainGeneration = 1;
		}
		m_RecreatePending = false;
		return true;
	}

	void VulkanContext::FinishFrame(VulkanFrameContext& frame) noexcept
	{
		frame.m_Active = false;
		m_ActiveFrame = nullptr;
	}

	void VulkanContext::Finalize() noexcept
	{
		if (m_ActiveFrame)
		{
			GGLAB_UNUSED(AbortFrame(*m_ActiveFrame));
		}
		if (m_Bootstrap && m_Bootstrap->m_FrameRuntime)
		{
			WaitIdle();
		}
		if (m_SwapChain)
		{
			m_SwapChain->Finalize();
		}
		RetireCompletedWork();
		m_Frames.clear();
		m_SwapChain.reset();
		m_DirectComputeContext.reset();
		m_GraphicsContext.reset();
		m_GpuProfiler.reset();
		m_Set0Frames.reset();
		m_DynamicUniformBuffer.reset();
		m_TransferManager.reset();
		m_PipelineSystem.reset();
		m_Bootstrap.reset();
		m_Initialized = false;
	}
}
