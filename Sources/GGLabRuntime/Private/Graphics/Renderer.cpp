#include "Graphics/Renderer.h"
#include "GGLabFoundation/Base/CoreMacros.h"
#include "GGLabRuntime/Core/Log/LogMacros.h"
#include "GGLabRuntime/Graphics/EnvironmentLightingControlBase.h"
#include "GGLabRuntime/Graphics/EnvironmentLightingViewBase.h"
#include "GGLabRuntime/Graphics/IBLCacheControlBase.h"
#include "GGLabRuntime/Graphics/IBLPreviewControlBase.h"
#include "GGLabRuntime/Graphics/IBLPreviewViewBase.h"
#include "GGLabRuntime/Graphics/PostProcess/PostProcessPreviewControlBase.h"
#include "GGLabRuntime/Graphics/PostProcess/PostProcessPreviewViewBase.h"
#include "GGLabRuntime/Graphics/Profiling/GpuProfilingControlBase.h"
#include "GGLabRuntime/Graphics/Profiling/GpuProfilingViewBase.h"
#include "GGLabRuntime/Graphics/ShadowPreviewViewBase.h"
#include "GGLabRuntime/Graphics/Asset/AssetUploadScheduler.h"
#include "GGLabRuntime/Graphics/Asset/AssetManager.h"
#include "Graphics/EnvironmentLightingSystem.h"
#include "Graphics/IBLBakeScheduler.h"
#include "Graphics/Pipeline/PipelineCache.h"
#include "Graphics/Pipeline/TemporalAACapability.h"
#include "Graphics/Pipeline/TemporalHistoryManager.h"
#include "Graphics/Pipeline/TemporalMotion.h"
#include "Graphics/Profiling/GpuProfiler.h"
#include "Graphics/RenderFrameBuilder.h"
#include "Graphics/RenderFrameGpuResources.h"
#include "Graphics/RenderSceneBuilder.h"
#include "GGLabRuntime/Graphics/RHI/RHIPipelineSystem.h"
#include "Graphics/Resource/RenderResourceRegistry.h"
#include "Graphics/SamplerRegistry.h"
#include "GGLabRuntime/Graphics/Shader/ShaderManager.h"
#include "ShaderArtifactRuntime/GGLabShaderPrograms.h"
#include "GGLabRuntime/Graphics/TransferManager.h"

#include <atomic>
#include <cstdint>
#include <memory>
#include <utility>

namespace gglab
{
	Renderer::Renderer() noexcept = default;

	namespace
	{
		void AddBindingSlot(RHIBindingLayoutDesc& desc, RHIBindingType type,
			RHIShaderStage visibility, uint32_t binding, uint32_t space, uint32_t count,
			uint32_t sizeInBytes, const char* debugName) noexcept
		{
			GGLAB_ASSERT(desc.m_SlotCount < RHIBindingLayoutDesc::MaxSlots);
			auto& slot = desc.m_Slots[desc.m_SlotCount++];
			slot.m_Type = type;
			slot.m_Visibility = visibility;
			slot.m_Binding = binding;
			slot.m_Space = space;
			slot.m_Count = count;
			slot.m_SizeInBytes = sizeInBytes;
			slot.m_DebugName = debugName;
		}
	}

	Renderer::~Renderer()
	{
		GGLAB_ASSERT_MSG(
			!m_HasActiveFrame, "Renderer destroyed while a render frame is still active.");
	}

	RenderSamplerAccess* Renderer::GetSamplerRegistry() const noexcept
	{
		return m_SamplerRegistry.get();
	}

	EnvironmentSourceControl* Renderer::GetEnvironmentSourceControl() const noexcept
	{
		return m_EnvironmentLightingSystem.get();
	}

	bool Renderer::Initialize(const CreateInfo& createInfo) noexcept
	{
		if (createInfo.m_RHIContextFactory == nullptr)
		{
			GGLAB_LOG_GRAPHICS_ERROR_ALWAYS(
				"Renderer initialization requires a host-supplied RHI context factory.");
			return false;
		}
		if (!createInfo.HasRequiredRuntimePaths())
		{
			GGLAB_LOG_GRAPHICS_ERROR_ALWAYS(
				"Renderer initialization requires a non-empty IBL cache root.");
			return false;
		}

		RHIContextDesc contextDesc{};
		contextDesc.m_Width = createInfo.m_Width;
		contextDesc.m_Height = createInfo.m_Height;
		contextDesc.m_AdapterSelector = createInfo.m_AdapterSelector;
		contextDesc.m_EnableDebugValidation = createInfo.m_EnableDebugValidation;
		m_RHIContext = createInfo.m_RHIContextFactory->CreateContext(contextDesc);
		if (!m_RHIContext)
		{
			GGLAB_LOG_GRAPHICS_ERROR_ALWAYS(
				"Renderer failed to create the explicitly selected RHI context.");
			return false;
		}

		auto* device = &m_RHIContext->GetDevice();
		m_AssetUploadScheduler =
			std::make_unique<AssetUploadScheduler>(AssetUploadScheduler::CreateInfo{
				.m_Device = device,
				.m_TransferManager = GetTransferManager(),
				});

		m_TransientResourcePool = std::make_unique<TransientResourcePool>(device);
		m_PersistentTexturePool = std::make_unique<PersistentTexturePool>(device);
		m_TemporalHistoryManager =
			std::make_unique<TemporalHistoryManager>(m_PersistentTexturePool.get());

		PipelineCache::CreateInfo pipelineCacheCreateInfo{
			.m_PipelineSystem = &m_RHIContext->GetPipelineSystem(),
			.m_ShaderManager = createInfo.m_ShaderManager,
		};
		m_PipelineCache = std::make_unique<PipelineCache>(pipelineCacheCreateInfo);

		SamplerRegistry::CreateInfo samplerRegistryCreateInfo{};
		samplerRegistryCreateInfo.m_Device = device;
		m_SamplerRegistry = std::make_unique<SamplerRegistry>(samplerRegistryCreateInfo);

		RenderResourceRegistry::CreateInfo renderResRegistryCreateInfo{};
		renderResRegistryCreateInfo.m_Device = device;
		renderResRegistryCreateInfo.m_TransientResourcePool = m_TransientResourcePool.get();
		renderResRegistryCreateInfo.m_SamplerRegistry = m_SamplerRegistry.get();
		m_RenderResRegistry = std::make_unique<RenderResourceRegistry>(renderResRegistryCreateInfo);
		m_RenderResRegistry->EnsureIblResources();

		EnvironmentLightingSystem::CreateInfo environmentLightingCreateInfo{};
		environmentLightingCreateInfo.m_RenderResourceRegistry = m_RenderResRegistry.get();
		m_EnvironmentLightingSystem =
			std::make_unique<EnvironmentLightingSystem>(environmentLightingCreateInfo);

		IBLBakeScheduler::CreateInfo iblBakeSchedulerCreateInfo{};
		iblBakeSchedulerCreateInfo.m_Device = device;
		iblBakeSchedulerCreateInfo.m_TaskSystem = createInfo.m_TaskSystem;
		iblBakeSchedulerCreateInfo.m_EnvironmentLightingSystem = m_EnvironmentLightingSystem.get();
		iblBakeSchedulerCreateInfo.m_RenderResourceRegistry = m_RenderResRegistry.get();
		iblBakeSchedulerCreateInfo.m_TransferManager = GetTransferManager();
		iblBakeSchedulerCreateInfo.m_GpuProfiler = GetGpuProfiler();
		iblBakeSchedulerCreateInfo.m_ShaderManager = createInfo.m_ShaderManager;
		iblBakeSchedulerCreateInfo.m_DerivedDataCacheDirectory =
			createInfo.m_IblDerivedDataCacheDirectory;
		m_IBLBakeScheduler = std::make_unique<IBLBakeScheduler>(iblBakeSchedulerCreateInfo);

		CreateCommonBindingLayout();
		InitializeGpuBuffers();

		const TemporalMotionFormatSupport motionSupport =
			QueryTemporalMotionFormatSupport(*device);
		m_TemporalAACapabilityStatus.m_MotionRenderTarget =
			motionSupport.m_RenderTarget.IsSupported();
		m_TemporalAACapabilityStatus.m_MotionShaderResource =
			motionSupport.m_ShaderResource.IsSupported();
		const TemporalAAResolvedColorFormatSupport resolvedColorSupport =
			QueryTemporalAAResolvedColorFormatSupport(*device);
		m_TemporalAACapabilityStatus.m_ResolvedColorRenderTarget =
			resolvedColorSupport.m_RenderTarget.IsSupported();
		m_TemporalAACapabilityStatus.m_ResolvedColorShaderResource =
			resolvedColorSupport.m_ShaderResource.IsSupported();
		m_TemporalAACapabilityStatus.m_ResolvedColorTypedUavStore =
			resolvedColorSupport.m_TypedUavStore.IsSupported();
		const TemporalHistoryFormatSupport historySupport =
			QueryTemporalHistoryFormatSupport(*device);
		m_TemporalAACapabilityStatus.m_HistoryColorShaderResource =
			historySupport.m_Color.m_ShaderResource.IsSupported();
		m_TemporalAACapabilityStatus.m_HistoryColorTypedUavStore =
			historySupport.m_Color.m_TypedUavStore.IsSupported();
		m_TemporalAACapabilityStatus.m_HistoryDepthShaderResource =
			historySupport.m_Depth.m_ShaderResource.IsSupported();
		m_TemporalAACapabilityStatus.m_HistoryDepthTypedUavStore =
			historySupport.m_Depth.m_TypedUavStore.IsSupported();
		m_TemporalAACapabilityStatus.m_BindingLayoutAvailable =
			m_CommonBindingLayout.IsValid();
		if (createInfo.m_ShaderManager)
		{
			const ShaderID coverageVertex =
				createInfo.m_ShaderManager->LoadProgram(shader_programs::ForwardCoverageVertex);
			const ShaderID velocityOpaque = createInfo.m_ShaderManager->LoadProgram(
				shader_programs::DepthPrepassVelocityOpaquePixel);
			const ShaderID velocityAlphaTest = createInfo.m_ShaderManager->LoadProgram(
				shader_programs::DepthPrepassVelocityAlphaTestPixel);
			m_TemporalAACapabilityStatus.m_VelocityProgramsAvailable =
				coverageVertex.IsValid() && velocityOpaque.IsValid() && velocityAlphaTest.IsValid();
		}

		m_FrameBuilder = std::make_unique<RenderFrameBuilder>();
		m_IsInitialized = true;
		return true;
	}

	void Renderer::Finalize() noexcept
	{
		if (!m_IsInitialized)
		{
			return;
		}

		GGLAB_ASSERT_MSG(!m_HasActiveFrame,
			"Renderer::Finalize called while a Renderer::Frame is still active.");

		m_IsSuspended.store(true, std::memory_order_relaxed);

		m_RHIContext->WaitIdle();
		m_AssetUploadScheduler->Finalize();

		m_IBLBakeScheduler.reset();
		m_EnvironmentLightingSystem.reset();
		m_RenderResRegistry.reset();
		m_SamplerRegistry.reset();
		m_PipelineCache.reset();
		m_TemporalHistoryManager->Shutdown();
		m_TemporalHistoryManager.reset();
		m_PersistentTexturePool.reset();
		m_TransientResourcePool.reset();
		m_AssetUploadScheduler.reset();

		m_SceneCB.reset();
		m_FrameBuilder.reset();
		m_ObjectTable.reset();
		m_MaterialTable.reset();
		m_LightTable.reset();
		m_ObjectSB.reset();
		m_MaterialSB.reset();
		m_LightSB.reset();
		m_ViewSB.reset();
		m_TemporalViewHistory.Invalidate();
		m_TemporalObjectHistory.Invalidate();
		m_TemporalAACapabilityStatus = {};

		m_RHIContext.reset();

		m_IsInitialized = false;
	}

	RenderFrame Renderer::BeginFrame() noexcept
	{
		GGLAB_ASSERT_MSG(m_IsInitialized, "Renderer::BeginFrame called before initialization.");
		GGLAB_ASSERT_MSG(
			!m_HasActiveFrame, "Renderer::BeginFrame called without ending the previous frame.");
		GGLAB_ASSERT_NOT_NULL(m_RHIContext.get());
		const RHIFrameBeginResult beginResult = m_RHIContext->BeginFrame();
		if (!beginResult.IsReady())
		{
			return RenderFrame(beginResult.GetStatus());
		}
		RHIFrameContext* rhiFrame = beginResult.GetFrame();
		GGLAB_ASSERT_NOT_NULL(rhiFrame);

		m_SceneCB->Tick();
		m_ViewSB->Tick();

		m_TransientResourcePool->Tick();
		m_PersistentTexturePool->Tick();
		m_AssetUploadScheduler->Tick();
		m_IBLBakeScheduler->Tick(m_LastSubmittedFencePoint);

		m_HasActiveFrame = true;
		m_FrameGpuResources = std::make_unique<RenderFrameGpuResources>();
		const uint64_t frameSerial = m_NextFrameSerial++;
		GGLAB_ASSERT_MSG(frameSerial != 0, "Renderer frame serial overflowed its valid range.");
		m_ActiveFrame = {};
		m_ActiveFrame.m_RHIFrame = rhiFrame;
		m_ActiveFrame.m_Serial = frameSerial;
		m_ActiveFrame.m_FrameSlotIndex = rhiFrame->GetFrameSlotIndex();
		m_ActiveFrame.m_BackBufferIndex = rhiFrame->GetBackBufferIndex();
		return MakeReadyFrame(this, frameSerial, m_ActiveFrame.m_FrameSlotIndex,
			m_ActiveFrame.m_BackBufferIndex);
	}

	TemporalFrameTransaction& Renderer::BeginTemporalFrame(Frame& frame,
		const ResolvedTemporalFramePlan& plan, uint32_t width, uint32_t height) noexcept
	{
		GGLAB_ASSERT_MSG(m_HasActiveFrame && frame.GetSerial() == m_ActiveFrame.m_Serial &&
			m_ActiveFrame.m_Phase == FramePhase::Begun,
			"Temporal frame planning requires the active begun render host frame.");
		m_ActiveFrame.m_TemporalTransaction.Begin(
			m_TemporalViewHistory, m_TemporalObjectHistory, plan, width, height,
			m_TemporalHistoryManager.get());
		return m_ActiveFrame.m_TemporalTransaction;
	}

	RenderFrameBuildResult Renderer::BuildFrame(
		const RenderFrameBuildRequest& request) noexcept
	{
		GGLAB_ASSERT_MSG(m_HasActiveFrame && request.m_FrameSerial == m_ActiveFrame.m_Serial &&
			m_ActiveFrame.m_Phase == FramePhase::Begun,
			"Frame building requires the active begun render host frame.");
		GGLAB_ASSERT_NOT_NULL(m_FrameBuilder.get());
		GGLAB_ASSERT_NOT_NULL(m_AttachedAssetManager);

		const RenderFrameBuilder::BuildInfo buildInfo{
			.m_World = request.m_World,
			.m_CameraRig = request.m_CameraRig,
			.m_Renderer = *this,
			.m_AssetManager = *m_AttachedAssetManager,
			.m_ShadowVisualizationSettings = request.m_ShadowVisualizationSettings,
			.m_ViewRenderProfile = request.m_ViewRenderProfile,
			.m_TemporalFramePlan = request.m_TemporalFramePlan,
			.m_TemporalFrameTransaction = &request.m_TemporalFrameTransaction,
			.m_DisplayViewId = request.m_DisplayViewId,
			.m_WindowWidth = request.m_WindowWidth,
			.m_WindowHeight = request.m_WindowHeight,
			.m_FrameSlotIndex = request.m_FrameSlotIndex,
			.m_BackBufferIndex = request.m_BackBufferIndex,
			.m_FrameSerial = request.m_FrameSerial,
		};
		RenderFrameBuilder::BuildResult built = m_FrameBuilder->Build(buildInfo);
		GGLAB_ASSERT_NOT_NULL(m_FrameGpuResources.get());
		m_FrameGpuResources->AdoptFrom(built.m_SceneGpuAllocations, built.m_UploadFencePoint);

		RenderFrameBuildResult result{};
		result.m_RenderViews = std::move(built.m_RenderViews);
		result.m_ViewRenderSettings = built.m_ViewRenderSettings;
		result.m_TemporalFramePlan = built.m_TemporalFramePlan;
		result.m_TemporalFrameTransaction = built.m_TemporalFrameTransaction;
		result.m_RenderScene = std::move(built.m_RenderScene);
		result.m_RenderQueues = built.m_RenderQueues;
		result.m_DebugDrawFrame = built.m_DebugDrawFrame;
		result.m_DebugDrawCullContext = built.m_DebugDrawCullContext;
		result.m_RenderSceneStatus = built.m_RenderSceneStatus;
		result.m_DisplayViewId = built.m_DisplayViewId;
		result.m_DirectionalShadowSettings =
			built.m_WorldData.GetMainDirectionalShadowSettings();
		result.m_ShadowVisualizationSettings = built.m_ShadowVisualizationSettings;
		result.m_FrameSlotIndex = built.m_FrameSlotIndex;
		result.m_BackBufferIndex = built.m_BackBufferIndex;
		result.m_FrameSerial = built.m_FrameSerial;
		return result;
	}

	void Renderer::InvalidateTemporalFrameAfterLateContractFailure(Frame& frame) noexcept
	{
		GGLAB_ASSERT_MSG(m_HasActiveFrame && frame.GetSerial() == m_ActiveFrame.m_Serial &&
			m_ActiveFrame.m_Phase == FramePhase::Begun,
			"Late temporal contract invalidation requires the active begun render host frame.");
		m_ActiveFrame.m_TemporalTransaction.Abort(m_LastSubmittedFencePoint);
		m_TemporalHistoryManager->Invalidate(
			TemporalHistoryResetReason::AvailabilityChanged, m_LastSubmittedFencePoint);
		m_TemporalViewHistory.Invalidate();
		m_TemporalObjectHistory.Invalidate();
	}

	void Renderer::InvalidateTemporalHistoryAfterResolveProgramChange() noexcept
	{
		GGLAB_ASSERT_MSG(!m_HasActiveFrame,
			"Temporal resolve program changes must be activated between renderer frames.");
		if (m_TemporalHistoryManager)
		{
			m_TemporalHistoryManager->Invalidate(
				TemporalHistoryResetReason::ResolveProgramChanged,
				m_LastSubmittedFencePoint);
		}
		m_TemporalViewHistory.Invalidate();
		m_TemporalObjectHistory.Invalidate();
	}

	void Renderer::Render(
		Frame& frame, RenderGraph& rg, const RenderFrameContext& renderContext) noexcept
	{
		GGLAB_ASSERT_MSG(m_IsInitialized, "Renderer::Render called before initialization.");
		GGLAB_ASSERT_MSG(m_HasActiveFrame && frame.GetSerial() == m_ActiveFrame.m_Serial,
			"Renderer::Render received a frame that is not the active render host frame.");
		GGLAB_ASSERT_MSG(m_ActiveFrame.m_Phase == FramePhase::Begun,
			"Renderer::Render requires an active frame begun by Renderer::BeginFrame.");
		GGLAB_ASSERT(renderContext.m_FrameSlotIndex == m_ActiveFrame.m_FrameSlotIndex);
		GGLAB_ASSERT(renderContext.m_BackBufferIndex == m_ActiveFrame.m_BackBufferIndex);
		GGLAB_ASSERT(renderContext.m_FrameSerial == m_ActiveFrame.m_Serial);

		m_ActiveFrame.m_RenderGraph = &rg;
		GGLAB_ASSERT_NOT_NULL(m_FrameGpuResources.get());

		// Window suspended do nothing
		if (m_IsSuspended.load(std::memory_order_relaxed))
		{
			return;
		}

		if (!GetSwapChain() || !GetSwapChain()->IsValid())
		{
			return;
		}

		// Wait Structured Buffer upload
		if (m_FrameGpuResources->m_UploadFencePoint.IsValid())
		{
			m_RHIContext->WaitForFence(
				RHIQueueType::Graphics, m_FrameGpuResources->m_UploadFencePoint);
		}

		RGExecuteContext executeContext{ RGBackendExecuteContext{
			.m_GraphicsCommandContext = &m_ActiveFrame.m_RHIFrame->GetGraphicsContext(),
			.m_DirectComputeCommandContext = &m_ActiveFrame.m_RHIFrame->GetDirectComputeContext(),
			.m_AsyncComputeCommandContext = nullptr,
		} };
		rg.Execute(executeContext);

		m_ActiveFrame.m_Phase = FramePhase::Recorded;
	}

	RHIFrameEndResult Renderer::EndFrame(Frame& frame) noexcept
	{
		GGLAB_ASSERT_MSG(m_IsInitialized, "Renderer::EndFrame called before initialization.");
		GGLAB_ASSERT_MSG(m_HasActiveFrame && frame.GetSerial() == m_ActiveFrame.m_Serial,
			"Renderer::EndFrame called without the matching active render host frame.");

		if (m_ActiveFrame.m_Phase != FramePhase::Recorded)
		{
			return RHIFrameEndResult::Fatal(AbortActiveFrame(frame.GetSerial()));
		}

		GGLAB_ASSERT_NOT_NULL(m_ActiveFrame.m_RHIFrame);
		GGLAB_ASSERT_NOT_NULL(m_ActiveFrame.m_RenderGraph);

		const RHIFrameEndResult result = m_RHIContext->EndFrame(*m_ActiveFrame.m_RHIFrame);
		const RHIFencePoint submittedFence = result.GetSubmittedFence();
		if (result.IsCompleted() && submittedFence.IsValid())
		{
			m_ActiveFrame.m_TemporalTransaction.CommitCompleted(submittedFence);
		}
		else
		{
			m_ActiveFrame.m_TemporalTransaction.InvalidateAfterFatal(submittedFence);
			m_TemporalHistoryManager->Invalidate(
				TemporalHistoryResetReason::FatalSubmission, submittedFence);
			m_TemporalViewHistory.Invalidate();
			m_TemporalObjectHistory.Invalidate();
		}
		if (submittedFence.IsValid())
		{
			m_LastSubmittedFencePoint = submittedFence;
			m_IBLBakeScheduler->OnFrameSubmitted(submittedFence);
		}
		else
		{
			m_IBLBakeScheduler->OnFrameAborted();
		}

		const RHIFencePoint retirementFence =
			submittedFence.IsValid() ? submittedFence : m_LastSubmittedFencePoint;
		if (retirementFence.IsValid())
		{
			GGLAB_ASSERT_NOT_NULL(m_FrameGpuResources.get());
			RetireSceneGpuAllocations(
				&m_FrameGpuResources->m_SceneGpuAllocations, retirementFence);
			m_ActiveFrame.m_RenderGraph->Retire(retirementFence);
		}

		EndFrameLifetime();
		return result;
	}

	void Renderer::AbortFrame(uint64_t frameSerial) noexcept
	{
		GGLAB_UNUSED(AbortActiveFrame(frameSerial));
	}

	RHIFencePoint Renderer::AbortActiveFrame(uint64_t frameSerial) noexcept
	{
		if (!m_HasActiveFrame || m_ActiveFrame.m_Serial != frameSerial)
		{
			return {};
		}
		if (m_IBLBakeScheduler)
		{
			m_IBLBakeScheduler->OnFrameAborted();
		}

		if (m_RHIContext && m_FrameGpuResources &&
			m_FrameGpuResources->m_UploadFencePoint.IsValid())
		{
			m_RHIContext->WaitForFence(
				RHIQueueType::Graphics, m_FrameGpuResources->m_UploadFencePoint);
		}

		if (m_RHIContext && m_ActiveFrame.m_RHIFrame)
		{
			const RHIFencePoint submittedFence =
				m_RHIContext->AbortFrame(*m_ActiveFrame.m_RHIFrame);
			if (submittedFence.IsValid())
			{
				m_LastSubmittedFencePoint = submittedFence;
			}
			const RHIFencePoint retirementFence =
				submittedFence.IsValid() ? submittedFence : m_LastSubmittedFencePoint;
			m_ActiveFrame.m_TemporalTransaction.Abort(retirementFence);
			if (retirementFence.IsValid())
			{
				if (m_FrameGpuResources)
				{
					RetireSceneGpuAllocations(
						&m_FrameGpuResources->m_SceneGpuAllocations, retirementFence);
				}

				if (m_ActiveFrame.m_RenderGraph)
				{
					m_ActiveFrame.m_RenderGraph->Retire(retirementFence);
				}
			}
			EndFrameLifetime();
			return submittedFence;
		}

		m_ActiveFrame.m_TemporalTransaction.Abort();
		EndFrameLifetime();
		return {};
	}

	void Renderer::EndFrameLifetime() noexcept
	{
		m_ActiveFrame = {};
		m_FrameGpuResources.reset();
		m_HasActiveFrame = false;
	}

	const EnvironmentLightingSettings& Renderer::GetEnvironmentLightingSettings() const noexcept
	{
		GGLAB_ASSERT_NOT_NULL(m_EnvironmentLightingSystem.get());
		return m_EnvironmentLightingSystem->GetSettings();
	}

	bool Renderer::ShouldInitializeBakeResources() const noexcept
	{
		return m_IBLBakeScheduler && m_IBLBakeScheduler->ShouldInitializeBakeResources();
	}

	uint64_t Renderer::GetBakingGeneration() const noexcept
	{
		return m_IBLBakeScheduler ? m_IBLBakeScheduler->GetBakingGeneration() : 0;
	}

	const IBLBakeConfig& Renderer::GetBakingConfig() const noexcept
	{
		GGLAB_ASSERT_NOT_NULL(m_IBLBakeScheduler.get());
		return m_IBLBakeScheduler->GetBakingConfig();
	}

	const IBLBakeStatus& Renderer::GetBakingStatus() const noexcept
	{
		GGLAB_ASSERT_NOT_NULL(m_IBLBakeScheduler.get());
		return m_IBLBakeScheduler->GetStatus();
	}

	IBLBakeStage Renderer::GetStageForRecording() const noexcept
	{
		return m_IBLBakeScheduler ? m_IBLBakeScheduler->GetStageForRecording()
								  : IBLBakeStage::Idle;
	}

	void Renderer::NotifyStageExecuted(IBLBakeStage stage, uint64_t generation) noexcept
	{
		GGLAB_ASSERT_NOT_NULL(m_IBLBakeScheduler.get());
		m_IBLBakeScheduler->NotifyStageExecuted(stage, generation);
	}

	void Renderer::NotifyBakeResourcesInitialized(uint64_t generation) noexcept
	{
		GGLAB_ASSERT_NOT_NULL(m_IBLBakeScheduler.get());
		m_IBLBakeScheduler->NotifyBakeResourcesInitialized(generation);
	}

	const EnvironmentTextureSource& Renderer::GetBakingSource() const noexcept
	{
		GGLAB_ASSERT_NOT_NULL(m_IBLBakeScheduler.get());
		return m_IBLBakeScheduler->GetBakingSource();
	}

	ArtifactCacheCoreStatistics Renderer::GetArtifactCacheStatistics() const noexcept
	{
		return m_IBLBakeScheduler ? m_IBLBakeScheduler->GetArtifactCacheStatistics()
								  : ArtifactCacheCoreStatistics{};
	}

	LocalDerivedDataStoreStatistics Renderer::GetDerivedDataStoreStatistics() const noexcept
	{
		return m_IBLBakeScheduler ? m_IBLBakeScheduler->GetDerivedDataStoreStatistics()
								  : LocalDerivedDataStoreStatistics{};
	}

	TemporalHistoryManagerDiagnostics Renderer::GetTemporalHistoryDiagnostics() const
	{
		return m_TemporalHistoryManager ? m_TemporalHistoryManager->GetDiagnostics()
										: TemporalHistoryManagerDiagnostics{};
	}

	void Renderer::RetireSceneGpuAllocations(
		RenderSceneGpuAllocations* allocations, const RHIFencePoint& fencePoint) noexcept
	{
		if (!allocations || allocations->IsEmpty())
		{
			return;
		}

		GGLAB_ASSERT_MSG(
			fencePoint.IsValid(), "Scene GPU allocations require a valid graphics fence point.");

		if (allocations->m_Views.IsValid())
		{
			m_ViewSB->Retire(&allocations->m_Views, fencePoint);
		}
		if (allocations->m_SceneConstants.IsValid())
		{
			m_SceneCB->Retire(&allocations->m_SceneConstants, fencePoint);
		}
		*allocations = {};
	}

	RHIBindingLayoutDesc Renderer::GetCommonBindingLayoutDesc() const noexcept
	{
		return BuildCommonRHIBindingLayoutDesc();
	}

	RHIBindingLayoutDesc Renderer::BuildCommonRHIBindingLayoutDesc() noexcept
	{
		RHIBindingLayoutDesc desc{};
		desc.m_DebugName = "RendererCommonBindingLayout";

		AddBindingSlot(
			desc, RHIBindingType::ConstantBuffer, RHIShaderStage::All, 0, 0, 1, 0, "SceneCB");
		AddBindingSlot(desc, RHIBindingType::PushConstants, RHIShaderStage::All, 1, 0, 1,
			MaxDrawConstantDWORDs * sizeof(uint32_t), "DrawConstants");
		AddBindingSlot(desc, RHIBindingType::PushConstants, RHIShaderStage::All, 2, 0, 1,
			MaxPassConstantDWORDs * sizeof(uint32_t), "PassConstants");
		AddBindingSlot(desc, RHIBindingType::ReadOnlyStorageBuffer, RHIShaderStage::All, 1, 0, 1, 0,
			"ObjectSB");
		AddBindingSlot(desc, RHIBindingType::ReadOnlyStorageBuffer, RHIShaderStage::All, 2, 0, 1, 0,
			"MaterialSB");
		AddBindingSlot(
			desc, RHIBindingType::ReadOnlyStorageBuffer, RHIShaderStage::All, 3, 0, 1, 0, "ViewSB");
		AddBindingSlot(desc, RHIBindingType::ReadOnlyStorageBuffer, RHIShaderStage::All, 4, 0, 1, 0,
			"LightSB");
		AddBindingSlot(desc, RHIBindingType::BindlessResourceTable, RHIShaderStage::All, 0, 0, 0, 0,
			"BindlessResources");
		AddBindingSlot(desc, RHIBindingType::BindlessSamplerTable, RHIShaderStage::All, 0, 0, 0, 0,
			"BindlessSamplers");
		return desc;
	}

	RenderGraph::CreateInfo Renderer::CreateRenderGraphCreateInfo() const noexcept
	{
		RenderGraph::CreateInfo rgCreateInfo{};
		rgCreateInfo.m_Device = GetDevice();
		rgCreateInfo.m_TransientResourcePool = m_TransientResourcePool.get();

		return rgCreateInfo;
	}

	void Renderer::OnResize(uint32_t width, uint32_t height) noexcept
	{
		if (!m_IsInitialized)
		{
			GGLAB_LOG_GRAPHICS_WARN("Renderer::OnResize. Renderer is not initialized.");
			return;
		}

		if (m_IsSuspended.load(std::memory_order_relaxed))
		{
			GGLAB_LOG_GRAPHICS_WARN("Renderer::OnResize ignored because renderer is suspended.");
			return;
		}

		if (!GetSwapChain() || !GetSwapChain()->IsValid())
		{
			GGLAB_LOG_GRAPHICS_WARN("Renderer::OnResize. SwapChain is invalid.");
			return;
		}

		if (width == 0 || height == 0)
		{
			GGLAB_LOG_GRAPHICS_WARN("Renderer::OnResize. Invalid resize resolution.");
			return;
		}

		m_RHIContext->Resize(width, height);
		m_TemporalHistoryManager->Invalidate(
			TemporalHistoryResetReason::ExtentChanged, m_LastSubmittedFencePoint);
	}

	void Renderer::OnSuspend() noexcept
	{
		m_IsSuspended.store(true, std::memory_order_relaxed);
	}

	void Renderer::OnResume() noexcept
	{
		if (m_TemporalHistoryManager)
		{
			m_TemporalHistoryManager->Invalidate(
				TemporalHistoryResetReason::Resume, m_LastSubmittedFencePoint);
		}
		m_TemporalViewHistory.Invalidate();
		m_TemporalObjectHistory.Invalidate();
		m_IsSuspended.store(false, std::memory_order_relaxed);
	}

	bool Renderer::IsSuspended() const noexcept
	{
		return m_IsSuspended.load(std::memory_order_relaxed);
	}

	void Renderer::CreateCommonBindingLayout() noexcept
	{
		const RHIBindingLayoutDesc commonBindingLayout = BuildCommonRHIBindingLayoutDesc();
		m_CommonBindingLayout =
			m_RHIContext->GetPipelineSystem().CreateBindingLayout(commonBindingLayout);
		GGLAB_ASSERT_MSG(m_CommonBindingLayout.IsValid(),
			"Renderer failed to create the common RHI binding layout.");
	}

	void Renderer::InitializeGpuBuffers() noexcept
	{
		// Small, per-frame CPU-visible constant allocations.
		{
			DynamicConstantBufferAllocator::CreateInfo createInfo{};
			createInfo.m_Device = GetDevice();
			createInfo.m_CapacityInBytes = static_cast<uint32_t>(sizeof(SceneGPU)) *
				m_RHIContext->GetFrameSlotCount() * 4;
			createInfo.m_DebugName = "Renderer.DynamicConstants";
			m_SceneCB = std::make_unique<DynamicConstantBufferAllocator>(createInfo);
		}

		// Persistent scene tables are replicated per frame slot. Each frame only updates
		// the safe physical version selected by its reusable slot.
		{
			PersistentStructuredBuffer<ObjectGPU>::CreateInfo objectSBCreateInfo{};
			objectSBCreateInfo.m_Device = GetDevice();
			objectSBCreateInfo.m_ElementCapacity = MaxObjectCapacity;
			objectSBCreateInfo.m_BufferCount = m_RHIContext->GetFrameSlotCount();
			objectSBCreateInfo.m_DebugName = "Renderer.PersistentObjects";
			m_ObjectSB =
				std::make_unique<PersistentStructuredBuffer<ObjectGPU>>(objectSBCreateInfo);
			m_ObjectTable = std::make_unique<PersistentStructuredBufferTable<uint64_t, ObjectGPU>>(
				MaxObjectCapacity, m_RHIContext->GetFrameSlotCount());

			PersistentStructuredBuffer<MaterialGPU>::CreateInfo materialSBCreateInfo{};
			materialSBCreateInfo.m_Device = GetDevice();
			materialSBCreateInfo.m_ElementCapacity = MaxMaterialCapacity;
			materialSBCreateInfo.m_BufferCount = m_RHIContext->GetFrameSlotCount();
			materialSBCreateInfo.m_DebugName = "Renderer.PersistentMaterials";
			m_MaterialSB =
				std::make_unique<PersistentStructuredBuffer<MaterialGPU>>(materialSBCreateInfo);
			m_MaterialTable =
				std::make_unique<PersistentStructuredBufferTable<RenderMaterialKey, MaterialGPU>>(
					MaxMaterialCapacity, m_RHIContext->GetFrameSlotCount());

			PersistentStructuredBuffer<LightGPU>::CreateInfo lightSBCreateInfo{};
			lightSBCreateInfo.m_Device = GetDevice();
			lightSBCreateInfo.m_ElementCapacity = MaxLightCapacity;
			lightSBCreateInfo.m_BufferCount = m_RHIContext->GetFrameSlotCount();
			lightSBCreateInfo.m_DebugName = "Renderer.PersistentLights";
			m_LightSB = std::make_unique<PersistentStructuredBuffer<LightGPU>>(lightSBCreateInfo);
			m_LightTable = std::make_unique<PersistentStructuredBufferTable<uint64_t, LightGPU>>(
				MaxLightCapacity, m_RHIContext->GetFrameSlotCount());

			// View data remains a small per-frame dynamic upload allocation.
			DynamicStructuredBufferAllocator<ViewGPU>::CreateInfo viewSBCreateInfo{};
			viewSBCreateInfo.m_Device = GetDevice();
			viewSBCreateInfo.m_ElementCapacity =
				MaxViewCapacity * m_RHIContext->GetFrameSlotCount();
			viewSBCreateInfo.m_DebugName = "Renderer.DynamicViews";
			m_ViewSB =
				std::make_unique<DynamicStructuredBufferAllocator<ViewGPU>>(viewSBCreateInfo);
		}
	}

	EnvironmentLightingViewBase* Renderer::GetEnvironmentLightingView() const noexcept
	{
		return m_EnvironmentLightingSystem.get();
	}

	EnvironmentLightingControlBase* Renderer::GetEnvironmentLightingControl() const noexcept
	{
		return m_EnvironmentLightingSystem.get();
	}

	IBLCacheControlBase* Renderer::GetIBLCacheControl() const noexcept
	{
		return m_IBLBakeScheduler ? &m_IBLBakeScheduler->GetCacheControl() : nullptr;
	}

	IBLPreviewViewBase* Renderer::GetIBLPreviewView() const noexcept
	{
		return m_RenderResRegistry.get();
	}

	IBLPreviewControlBase* Renderer::GetIBLPreviewControl() const noexcept
	{
		return m_RenderResRegistry.get();
	}

	PostProcessPreviewViewBase* Renderer::GetPostProcessPreviewView() const noexcept
	{
		return m_RenderResRegistry.get();
	}

	PostProcessPreviewControlBase* Renderer::GetPostProcessPreviewControl() const noexcept
	{
		return m_RenderResRegistry.get();
	}

	ShadowPreviewViewBase* Renderer::GetShadowPreviewView() const noexcept
	{
		return m_RenderResRegistry.get();
	}

	GpuProfilingViewBase* Renderer::GetGpuProfilingView() const noexcept
	{
		GpuProfiler* profiler = m_RHIContext ? m_RHIContext->GetGpuProfiler() : nullptr;
		return profiler;
	}

	GpuProfilingControlBase* Renderer::GetGpuProfilingControl() const noexcept
	{
		GpuProfiler* profiler = m_RHIContext ? m_RHIContext->GetGpuProfiler() : nullptr;
		return profiler;
	}

	void Renderer::AttachAssetManager(AssetManager& assetManager) noexcept
	{
		m_AttachedAssetManager = &assetManager;
		if (m_IBLBakeScheduler)
		{
			m_IBLBakeScheduler->AttachAssetManager(assetManager);
		}
	}

	void Renderer::DetachAssetManager() noexcept
	{
		m_AttachedAssetManager = nullptr;
		if (m_IBLBakeScheduler)
		{
			m_IBLBakeScheduler->DetachAssetManager();
		}
	}
}
