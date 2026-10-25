#include "Graphics/Renderer.h"
#include "GGLabRuntime/Graphics/Pipeline/SceneBindingLayouts.h"
#include "GGLabRuntime/Graphics/RenderParameters.h"
#include "GGLabRuntime/Graphics/GPUStructures.h"
#include "Graphics/AtmosphereSystem.h"
#include "Graphics/Capture/FrameCaptureService.h"
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
#include "GGLabRuntime/Graphics/Asset/AssetUploadScheduling.h"
#include "GGLabRuntime/Graphics/Asset/AssetManager.h"
#include "Graphics/EnvironmentLightingSystem.h"
#include "Graphics/IBLBakeScheduler.h"
#include "Graphics/Pipeline/GTAOCapability.h"
#include "Graphics/Pipeline/GTAOTemporalHistory.h"
#include "Graphics/Pipeline/PipelineCache.h"
#include "Graphics/Pipeline/TemporalAACapability.h"
#include "Graphics/Pipeline/TemporalHistoryManager.h"
#include "Graphics/Pipeline/TemporalReferenceAccumulator.h"
#include "Graphics/Pipeline/TemporalMotion.h"
#include "Graphics/Profiling/GpuProfiler.h"
#include "Graphics/RenderFrameBuilder.h"
#include "Graphics/RenderFrameOutcome.h"
#include "Graphics/RenderSceneBuilder.h"
#include "GGLabRuntime/Graphics/RHI/RHIPipelineSystem.h"
#include "GGLabRuntime/Graphics/RHI/RHIUpload.h"
#include "Graphics/Resource/RenderResourceRegistry.h"
#include "Graphics/SamplerRegistry.h"
#include "GGLabRuntime/Graphics/Shader/ShaderManager.h"
#include "GGLabRuntime/Graphics/TransferManager.h"

#include <atomic>
#include <cstdint>
#include <memory>
#include <utility>

namespace gglab
{
	Renderer::Renderer() noexcept = default;

	Renderer::~Renderer()
	{
		RevokeRenderServices();
		GGLAB_ASSERT_MSG(
			!m_HasActiveFrame, "Renderer destroyed while a render frame is still active.");
	}

	RenderServicesView Renderer::PublishRenderServices(const RenderServices& bindings) noexcept
	{
		m_ServicePublication = std::make_unique<RenderServicesPublication>(bindings);
		return m_ServicePublication->GetView();
	}

	void Renderer::CheckServiceLifecycleAccess() const noexcept
	{
		if (m_ServicePublication)
		{
			m_ServicePublication->CheckLifecycleAccess();
		}
	}

	void Renderer::RevokeRenderServices() noexcept
	{
		if (m_ServicePublication)
		{
			m_ServicePublication->Revoke();
		}
	}

	RenderSamplerAccess* Renderer::GetSamplerRegistry() const noexcept
	{
		return m_SamplerRegistry.get();
	}

	WorldLightingControlBase* Renderer::GetWorldLightingControl() const noexcept
	{
		return m_EnvironmentLightingSystem.get();
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
		contextDesc.m_EnableGpuBasedValidation = createInfo.m_EnableGpuBasedValidation;
		m_RHIContext = createInfo.m_RHIContextFactory->CreateContext(contextDesc);
		if (!m_RHIContext)
		{
			GGLAB_LOG_GRAPHICS_ERROR_ALWAYS(
				"Renderer failed to create the explicitly selected RHI context.");
			return false;
		}

		auto* device = &m_RHIContext->GetDevice();
		AssetUploadSchedulerInstance uploadSchedulerInstance =
			CreateAssetUploadScheduler(AssetUploadSchedulerCreateInfo{
				.m_Device = device,
				.m_TransferManager = GetTransferManager(),
				});
		m_AssetUploadScheduler = std::move(uploadSchedulerInstance.m_Scheduling);

		m_TransientResourcePool = std::make_unique<TransientResourcePool>(device);
		m_PersistentTexturePool = std::make_unique<PersistentTexturePool>(device);
		m_Atmosphere = std::make_unique<AtmosphereSystem>(device, m_PersistentTexturePool.get());
		m_BakeAtmosphere = std::make_unique<AtmosphereSystem>(device, m_PersistentTexturePool.get());
		m_TemporalHistoryManager =
			std::make_unique<TemporalHistoryManager>(m_PersistentTexturePool.get());
		m_TemporalReferenceAccumulator =
			std::make_unique<TemporalReferenceAccumulator>(m_PersistentTexturePool.get());
		m_GTAOTemporalHistory =
			std::make_unique<GTAOTemporalHistory>(m_PersistentTexturePool.get());

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
		iblBakeSchedulerCreateInfo.m_Atmosphere = m_Atmosphere.get();
		iblBakeSchedulerCreateInfo.m_BakeAtmosphere = m_BakeAtmosphere.get();
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
		m_TemporalAACapabilityStatus.m_HistoryReliabilityShaderResource =
			historySupport.m_Reliability.m_ShaderResource.IsSupported();
		m_TemporalAACapabilityStatus.m_HistoryReliabilityTypedUavStore =
			historySupport.m_Reliability.m_TypedUavStore.IsSupported();
		m_GTAOCapabilityStatus = QueryGTAOCapabilityStatus(*device);

		m_FrameBuilder = std::make_unique<RenderFrameBuilder>();
		m_FrameCapture = std::make_unique<FrameCaptureService>(m_RHIContext->GetDevice());
		m_FrameCapture->ConfigureNonFiniteScans(*createInfo.m_ShaderManager, *m_PipelineCache,
			m_RHIContext->GetPipelineSystem());
		if (createInfo.m_ShaderManager->HasNonFiniteCounters())
		{
			if (!device->SupportsPixelStorageAtomics())
			{
				GGLAB_LOG_GRAPHICS_ERROR("Non-finite instrumentation requires pixel storage atomics.");
				return false;
			}
			if (!m_FrameCapture->EnableNonFiniteProduction())
			{
				GGLAB_LOG_GRAPHICS_ERROR("Capture limits cannot hold production counter evidence.");
				return false;
			}
		}
		m_IsInitialized = true;
		return true;
	}

	void Renderer::Finalize() noexcept
	{
		RevokeRenderServices();
		if (!m_IsInitialized)
		{
			return;
		}

		GGLAB_ASSERT_MSG(!m_HasActiveFrame,
			"Renderer::Finalize called while a render frame is still active.");

		m_IsSuspended.store(true, std::memory_order_relaxed);

		// Budget evidence for the session. The batch byte and page caps charge leased page
		// capacity; reserved bytes show how much of it carried payload and padding.
		const RHIUploadStatistics uploads = m_RHIContext->GetUploadStatistics();
		GGLAB_LOG_GRAPHICS_INFO_ALWAYS(
			"Upload statistics: per-batch high-water leased={} B, pages={}, reserved={} B; "
			"pool pages={}, capacity={} B; batches submitted={}, cancelled={}; failures "
			"oversized={}, batch-bytes={}, pool-bytes={}, page-count={}, creation={}, mapping={}, "
			"publication={}.",
			uploads.m_HighWaterLeasedBytesPerBatch, uploads.m_HighWaterPagesPerBatch,
			uploads.m_HighWaterReservedBytesPerBatch, uploads.m_Total.m_PageCount,
			uploads.m_Total.m_CapacityInBytes, uploads.m_SubmittedBatchCount,
			uploads.m_CancelledBatchCount, uploads.m_OversizedUploadFailures,
			uploads.m_BatchByteBudgetFailures, uploads.m_PoolByteBudgetFailures,
			uploads.m_PageCountFailures, uploads.m_PageCreationFailures,
			uploads.m_PageMappingFailures, uploads.m_PublicationFailures);

		if (m_Poisoned || m_RHIContext->IsPoisoned() || m_RHIContext->IsDeviceLost())
		{
			m_AssetUploadScheduler->StopGpuWork();
		}
		const RHITeardownState teardown = QuiesceRHIForTeardown(*m_RHIContext);
		if (teardown == RHITeardownState::Unverified)
		{
			AbandonGpuOwners();
			m_IsInitialized = false;
			return;
		}
		if (teardown == RHITeardownState::DeviceLost)
		{
			m_AssetUploadScheduler->StopGpuWork();
		}
		// No queue still runs work: every owner below may release. Captures publish their
		// rows only after verified idle; after device loss they fail without reading.
		m_FrameCapture->Shutdown();
		m_AssetUploadScheduler->Finalize();

		m_IBLBakeScheduler.reset();
		m_EnvironmentLightingSystem.reset();
		m_RenderResRegistry.reset();
		m_SamplerRegistry.reset();
		m_PipelineCache.reset();
		m_Atmosphere->Shutdown();
		m_BakeAtmosphere->Shutdown();
		m_BakeAtmosphere.reset();
		m_Atmosphere.reset();
		m_TemporalHistoryManager->Shutdown();
		m_TemporalHistoryManager.reset();
		m_TemporalReferenceAccumulator->Release(m_LastSubmittedFencePoint);
		m_TemporalReferenceAccumulator.reset();
		m_GTAOTemporalHistory->Release(m_LastSubmittedFencePoint);
		m_GTAOTemporalHistory.reset();
		m_PersistentTexturePool.reset();
		m_TransientResourcePool.reset();
		m_AssetUploadScheduler.reset();

		m_FrameBuilder.reset();
		m_ObjectTable.reset();
		m_MaterialTable.reset();
		m_LightTable.reset();
		m_ObjectSB.reset();
		m_MaterialSB.reset();
		m_LightSB.reset();
		m_TemporalViewHistory.Invalidate();
		m_TemporalObjectHistory.Invalidate();
		m_TemporalAACapabilityStatus = {};
		m_GTAOCapabilityStatus = {};

		m_CommonBindingLayout = {};
		m_RHIContext.reset();

		m_IsInitialized = false;
	}

	RenderFrame Renderer::BeginFrame() noexcept
	{
		CheckServiceLifecycleAccess();
		GGLAB_ASSERT_MSG(m_IsInitialized, "Renderer::BeginFrame called before initialization.");
		GGLAB_ASSERT_MSG(
			!m_HasActiveFrame, "Renderer::BeginFrame called without ending the previous frame.");
		GGLAB_ASSERT_NOT_NULL(m_RHIContext.get());
		if (m_Poisoned)
		{
			return RenderFrame(RHIFrameBeginStatus::Fatal);
		}
		m_FrameCapture->CollectCompleted();
		const RHIFrameBeginResult beginResult = m_RHIContext->BeginFrame();
		if (!beginResult.IsReady())
		{
			return RenderFrame(beginResult.GetStatus());
		}
		RHIFrameContext* rhiFrame = beginResult.GetFrame();
		GGLAB_ASSERT_NOT_NULL(rhiFrame);

		m_TransientResourcePool->Tick();
		m_PersistentTexturePool->Tick();
		m_AssetUploadScheduler->Tick();

		m_HasActiveFrame = true;
		const uint64_t frameSerial = m_NextFrameSerial++;
		GGLAB_ASSERT_MSG(frameSerial != 0, "Renderer frame serial overflowed its valid range.");
		if (GpuProfiler* profiler = m_RHIContext->GetGpuProfiler())
		{
			profiler->SetRenderFrameSerial(frameSerial);
		}
		m_ActiveFrame = {};
		m_ActiveFrame.m_RHIFrame = rhiFrame;
		m_ActiveFrame.m_Serial = frameSerial;
		m_ActiveFrame.m_FrameSlotIndex = rhiFrame->GetFrameSlotIndex();
		m_ActiveFrame.m_BackBufferIndex = rhiFrame->GetBackBufferIndex();
		return MakeReadyFrame(this, frameSerial, m_ActiveFrame.m_FrameSlotIndex,
			m_ActiveFrame.m_BackBufferIndex);
	}

	TemporalFrameTransaction& Renderer::BeginTemporalFrame(RenderFrame& frame,
		const ResolvedTemporalFramePlan& plan, const ViewResolution& resolution,
		float scenePreExposure,
		const std::optional<TemporalReferenceSample>& referenceSample) noexcept
	{
		GGLAB_ASSERT_MSG(m_HasActiveFrame && frame.GetSerial() == m_ActiveFrame.m_Serial &&
			m_ActiveFrame.m_Phase == FramePhase::Begun,
			"Temporal frame planning requires the active begun render host frame.");
		GGLAB_ASSERT_MSG(referenceSample.has_value() ==
			plan.GetConsumer(TemporalConsumer::Reference).m_Requested,
			"The temporal plan must be resolved with the frame's reference request.");
		const std::optional<TemporalReferenceSample> sample =
			plan.IsConsumerActive(TemporalConsumer::Reference) ? referenceSample : std::nullopt;
		// A sample the accumulator cannot take leaves the frame without an accumulator,
		// which the pipeline reports as a contract failure.
		// The reference averages the display-domain scene it accumulates.
		const bool canAccumulate = m_TemporalReferenceAccumulator->BeginFrame(sample,
			resolution.m_Display.m_Width, resolution.m_Display.m_Height,
			m_LastSubmittedFencePoint);
		// Temporal GTAO history follows the half extent of the render domain. A history that
		// cannot be allocated leaves the frame without one, which the pipeline reports as a
		// contract failure.
		const bool ambientOcclusionHistory = plan.UsesAmbientOcclusionHistory();
		const bool canAccumulateAmbientOcclusion = m_GTAOTemporalHistory->BeginFrame(
			ambientOcclusionHistory,
			MakeGTAOHalfResolutionExtent(resolution.m_Render.m_Width, resolution.m_Render.m_Height),
			m_LastSubmittedFencePoint);
		m_ActiveFrame.m_TemporalTransaction.Begin(
			m_TemporalViewHistory, m_TemporalObjectHistory, plan, resolution,
			m_TemporalHistoryManager.get(), scenePreExposure, sample,
			sample && canAccumulate ? m_TemporalReferenceAccumulator.get() : nullptr,
			ambientOcclusionHistory && canAccumulateAmbientOcclusion ?
				m_GTAOTemporalHistory.get() :
				nullptr);
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
		GGLAB_ASSERT_NOT_NULL(m_ActiveFrame.m_RHIFrame);

		const RenderFrameBuilder::BuildInfo buildInfo{
			.m_World = request.m_World,
			.m_CameraRig = request.m_CameraRig,
			.m_Renderer = *this,
			.m_AssetManager = *m_AttachedAssetManager,
			.m_Uploads = m_ActiveFrame.m_RHIFrame->GetUploads(),
			.m_ShadowVisualizationSettings = request.m_ShadowVisualizationSettings,
			.m_ViewRenderProfile = request.m_ViewRenderProfile,
			.m_DisplayViewSettings = request.m_DisplayViewSettings,
			.m_TemporalFramePlan = request.m_TemporalFramePlan,
			.m_TemporalFrameTransaction = &request.m_TemporalFrameTransaction,
			.m_DisplayViewId = request.m_DisplayViewId,
			.m_ViewResolution = request.m_ViewResolution,
			.m_FrameSlotIndex = request.m_FrameSlotIndex,
			.m_BackBufferIndex = request.m_BackBufferIndex,
			.m_FrameSerial = request.m_FrameSerial,
		};
		RenderFrameBuilder::BuildResult built = m_FrameBuilder->Build(buildInfo);
		// Keep the transfer dependency even when the caller rejects the built CPU frame.
		// A later build with no dirty table ranges must not erase an earlier dependency.
		if (built.m_UploadFencePoint.IsValid())
		{
			GGLAB_ASSERT_MSG(!m_ActiveFrame.m_UploadFencePoint.IsValid() ||
				m_ActiveFrame.m_UploadFencePoint == built.m_UploadFencePoint,
				"A render frame cannot adopt resources from different upload submissions.");
			m_ActiveFrame.m_UploadFencePoint = built.m_UploadFencePoint;
		}

		RenderFrameBuildResult result{};
		result.m_RenderViews = std::move(built.m_RenderViews);
		result.m_ViewRenderSettings = built.m_ViewRenderSettings;
		result.m_TemporalFramePlan = built.m_TemporalFramePlan;
		result.m_TemporalFrameTransaction = built.m_TemporalFrameTransaction;
		result.m_RenderScene = std::move(built.m_RenderScene);
		result.m_FrameConstants = built.m_FrameConstants;
		result.m_Uploads = built.m_Uploads;
		result.m_RenderQueues = std::move(built.m_RenderQueues);
		result.m_DirectionalShadowFramePlan = std::move(built.m_DirectionalShadowFramePlan);
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

	void Renderer::InvalidateTemporalFrameAfterLateContractFailure(RenderFrame& frame) noexcept
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
		RenderFrame& frame, RenderGraph& rg, const RenderFrameContext& renderContext) noexcept
	{
		GGLAB_ASSERT_MSG(m_IsInitialized, "Renderer::Render called before initialization.");
		GGLAB_ASSERT_MSG(m_HasActiveFrame && frame.GetSerial() == m_ActiveFrame.m_Serial,
			"Renderer::Render received a frame that is not the active render host frame.");
		GGLAB_ASSERT_MSG(m_ActiveFrame.m_Phase == FramePhase::Begun,
			"Renderer::Render requires an active frame begun by Renderer::BeginFrame.");
		GGLAB_ASSERT(renderContext.m_FrameSlotIndex == m_ActiveFrame.m_FrameSlotIndex);
		GGLAB_ASSERT(renderContext.m_BackBufferIndex == m_ActiveFrame.m_BackBufferIndex);
		GGLAB_ASSERT(renderContext.m_FrameSerial == m_ActiveFrame.m_Serial);
		struct DeferredCommitSeal
		{
			RGDeferredCommitList& m_Commits;
			~DeferredCommitSeal() noexcept { m_Commits.Seal(); }
		};
		const DeferredCommitSeal commitSeal{ m_ActiveFrame.m_DeferredCommits };

		m_ActiveFrame.m_RenderGraph = &rg;

		// Window suspended do nothing
		if (m_IsSuspended.load(std::memory_order_relaxed))
		{
			return;
		}

		if (!GetSwapChain() || !GetSwapChain()->IsValid())
		{
			return;
		}

		// Wait Structured Buffer upload. Without the queue wait the graph could read uploads
		// that have not landed, so the frame stays unrecorded and EndFrame aborts it.
		if (m_ActiveFrame.m_UploadFencePoint.IsValid() &&
			!m_RHIContext->WaitForFence(
				RHIQueueType::Graphics, m_ActiveFrame.m_UploadFencePoint))
		{
			GGLAB_LOG_GRAPHICS_ERROR("Renderer could not order the frame after its uploads.");
			return;
		}

		RGExecuteContext executeContext{ m_ActiveFrame.m_DeferredCommits, RGBackendExecuteContext{
			.m_GraphicsCommandContext = &m_ActiveFrame.m_RHIFrame->GetGraphicsContext(),
			.m_DirectComputeCommandContext = &m_ActiveFrame.m_RHIFrame->GetDirectComputeContext(),
			.m_AsyncComputeCommandContext = nullptr,
		} };
		m_FrameCapture->BeginGraphRecording(m_ActiveFrame.m_Serial, rg);
		rg.Execute(executeContext);
		m_FrameCapture->EndGraphRecording(m_ActiveFrame.m_Serial, rg);

		m_ActiveFrame.m_Phase = FramePhase::Recorded;
	}

	RHIFrameEndResult Renderer::EndFrame(RenderFrame& frame) noexcept
	{
		GGLAB_ASSERT_MSG(m_IsInitialized, "Renderer::EndFrame called before initialization.");
		GGLAB_ASSERT_MSG(m_HasActiveFrame && frame.GetSerial() == m_ActiveFrame.m_Serial,
			"Renderer::EndFrame called without the matching active render host frame.");

		if (m_ActiveFrame.m_Phase != FramePhase::Recorded || m_ActiveFrame.m_DeferredCommits.HasFailed())
		{
			return AbortActiveFrame(frame.GetSerial());
		}

		GGLAB_ASSERT_NOT_NULL(m_ActiveFrame.m_RHIFrame);
		GGLAB_ASSERT_NOT_NULL(m_ActiveFrame.m_RenderGraph);

		const RHIFrameEndResult result = m_RHIContext->EndFrame(*m_ActiveFrame.m_RHIFrame);
		SettleFrame(ResolveRenderFrameOutcome(
			RenderFrameEndPath::EndFrame, m_ActiveFrame.m_Serial, result));
		// Settlement-time violations cannot undo submitted work or settle participants twice.
		const RHIFrameEndResult settledResult = result.IsCompleted() && m_ActiveFrame.m_DeferredCommits.HasFailed() ?
			RHIFrameEndResult::Fatal(result.GetSubmittedFence()) : result;
		EndFrameLifetime();
		return settledResult;
	}

	void Renderer::AbortFrame(uint64_t frameSerial) noexcept
	{
		GGLAB_UNUSED(AbortActiveFrame(frameSerial));
	}

	RHIFrameEndResult Renderer::AbortActiveFrame(uint64_t frameSerial) noexcept
	{
		if (!m_HasActiveFrame || m_ActiveFrame.m_Serial != frameSerial)
		{
			return RHIFrameEndResult::Fatal();
		}
		if (m_RHIContext && m_ActiveFrame.m_UploadFencePoint.IsValid() &&
			!m_RHIContext->WaitForFence(RHIQueueType::Graphics, m_ActiveFrame.m_UploadFencePoint))
		{
			// Unlike Render, abort can tolerate the missing order: the frame's own command lists
			// never execute and nothing retired below takes part in the scene-table upload. Its
			// staging retires on the transfer timeline and its destination tables outlive the
			// frame.
			GGLAB_LOG_GRAPHICS_WARN(
				"Renderer aborted a frame whose abort fence is not ordered after its uploads.");
		}

		if (m_RHIContext && m_ActiveFrame.m_RHIFrame)
		{
			const RHIFrameEndResult result = m_RHIContext->AbortFrame(*m_ActiveFrame.m_RHIFrame);
			SettleFrame(ResolveRenderFrameOutcome(
				RenderFrameEndPath::AbortActiveFrame, frameSerial, result));
			EndFrameLifetime();
			return result;
		}

		SettleFrame(ResolveRenderFrameOutcome(RenderFrameEndPath::AbortActiveFrame, frameSerial));
		EndFrameLifetime();
		return RHIFrameEndResult::Fatal();
	}

	void Renderer::SettleFrame(const RenderFrameOutcome& outcome) noexcept
	{
		GGLAB_ASSERT(m_HasActiveFrame && outcome.m_FrameSerial == m_ActiveFrame.m_Serial);
		const RHIFencePoint fence = outcome.m_Fence;
		switch (outcome.m_Kind)
		{
		case RenderFrameOutcomeKind::Submitted:
		case RenderFrameOutcomeKind::Failed:
		{
			// Preserve EndFrame order: capture, live/bake atmosphere, temporal histories,
			// last fence and IBL, then graph retirement. Submitted does not mean GPU idle.
			const bool submitted = outcome.m_Kind == RenderFrameOutcomeKind::Submitted;
			if (submitted && fence.IsValid())
			{
				m_FrameCapture->OnFrameSubmitted(outcome.m_FrameSerial, fence);
			}
			else
			{
				m_FrameCapture->OnFrameSubmissionFailed(outcome.m_FrameSerial);
			}
			m_Atmosphere->EndFrame(submitted, fence);
			m_BakeAtmosphere->EndFrame(submitted, fence);
			if (submitted && fence.IsValid())
			{
				m_ActiveFrame.m_TemporalTransaction.CommitCompleted(fence);
			}
			else
			{
				m_ActiveFrame.m_TemporalTransaction.InvalidateAfterFatal(fence);
				m_TemporalHistoryManager->Invalidate(TemporalHistoryResetReason::FatalSubmission, fence);
				m_TemporalViewHistory.Invalidate();
				m_TemporalObjectHistory.Invalidate();
			}
			if (fence.IsValid())
			{
				m_LastSubmittedFencePoint = fence;
				if (submitted)
				{
					m_IBLBakeScheduler->OnFrameSubmitted(fence);
				}
				else
				{
					m_IBLBakeScheduler->OnFrameAborted(fence);
				}
			}
			else
			{
				m_IBLBakeScheduler->OnFrameAborted();
			}
			const RHIFencePoint retirementFence = fence.IsValid() ? fence : m_LastSubmittedFencePoint;
			m_ActiveFrame.m_RenderGraph->Retire(retirementFence);
			break;
		}
		case RenderFrameOutcomeKind::Discarded:
		{
			// Preserve abort order: live/bake atmosphere, IBL, last fence, temporal,
			// graph, capture. RHI abort must record resource uses before capture releases.
			m_Atmosphere->EndFrame(false, fence);
			m_BakeAtmosphere->EndFrame(false, fence);
			m_IBLBakeScheduler->OnFrameAborted(fence);
			if (m_RHIContext && m_ActiveFrame.m_RHIFrame)
			{
				if (fence.IsValid())
				{
					m_LastSubmittedFencePoint = fence;
				}
				const RHIFencePoint retirementFence = fence.IsValid() ? fence : m_LastSubmittedFencePoint;
				m_ActiveFrame.m_TemporalTransaction.Abort(retirementFence);
				if (m_ActiveFrame.m_RenderGraph)
				{
					m_ActiveFrame.m_RenderGraph->Retire(retirementFence);
				}
			}
			else
			{
				// No RHI frame means no graph allocations to retire and no retention fence.
				m_ActiveFrame.m_TemporalTransaction.Abort();
			}
			m_FrameCapture->OnFrameAborted(outcome.m_FrameSerial);
			break;
		}
		case RenderFrameOutcomeKind::CompletionUnknown:
		{
			// Preserve retention order: poison, capture, live/bake atmosphere, IBL, temporal.
			// Nothing the frame can read is released or returned to the graph pool. Finalize
			// releases retained owners only after verified quiescence or confirmed device loss.
			GGLAB_LOG_GRAPHICS_ERROR_ALWAYS(
				"Renderer retained a frame whose GPU completion is unknown; no further frame begins.");
			m_Poisoned = true;
			m_FrameCapture->OnFrameCompletionUnknown(outcome.m_FrameSerial);
			m_Atmosphere->RetainAfterUnknownCompletion();
			m_BakeAtmosphere->RetainAfterUnknownCompletion();
			m_IBLBakeScheduler->OnFrameAborted();
			m_ActiveFrame.m_TemporalTransaction.Abort();
			m_TemporalViewHistory.Invalidate();
			m_TemporalObjectHistory.Invalidate();
			break;
		}
		}
		// Plain CPU commits settle after every GPU-owning participant, in enqueue order.
		if (outcome.m_Kind == RenderFrameOutcomeKind::Submitted)
		{
			m_ActiveFrame.m_DeferredCommits.CommitAll({ outcome.m_FrameSerial, fence });
		}
		else
		{
			m_ActiveFrame.m_DeferredCommits.Discard();
		}
	}

	void Renderer::AbandonGpuOwners() noexcept
	{
		// Destroying an owner could free memory the GPU still reads. Process exit reclaims the
		// abandoned owners; the capture service stays to publish its failures.
		m_FrameCapture->Abandon("The render host finalized before GPU idle could be verified.");
		GGLAB_UNUSED(m_IBLBakeScheduler.release());
		GGLAB_UNUSED(m_EnvironmentLightingSystem.release());
		GGLAB_UNUSED(m_RenderResRegistry.release());
		GGLAB_UNUSED(m_SamplerRegistry.release());
		GGLAB_UNUSED(m_PipelineCache.release());
		GGLAB_UNUSED(m_Atmosphere.release());
		GGLAB_UNUSED(m_BakeAtmosphere.release());
		GGLAB_UNUSED(m_TemporalHistoryManager.release());
		GGLAB_UNUSED(m_TemporalReferenceAccumulator.release());
		GGLAB_UNUSED(m_GTAOTemporalHistory.release());
		GGLAB_UNUSED(m_PersistentTexturePool.release());
		GGLAB_UNUSED(m_TransientResourcePool.release());
		GGLAB_UNUSED(m_AssetUploadScheduler.release());
		GGLAB_UNUSED(m_ObjectTable.release());
		GGLAB_UNUSED(m_MaterialTable.release());
		GGLAB_UNUSED(m_LightTable.release());
		GGLAB_UNUSED(m_ObjectSB.release());
		GGLAB_UNUSED(m_MaterialSB.release());
		GGLAB_UNUSED(m_LightSB.release());
		GGLAB_UNUSED(m_RHIContext.release());
	}

	void Renderer::EndFrameLifetime() noexcept
	{
		m_ActiveFrame.m_DeferredCommits.Discard();
		m_ActiveFrame = {};
		m_HasActiveFrame = false;
	}

	const EnvironmentLightingSettings& Renderer::GetEnvironmentLightingSettings() const noexcept
	{
		GGLAB_ASSERT_NOT_NULL(m_EnvironmentLightingSystem.get());
		return m_EnvironmentLightingSystem->GetRenderSettings();
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

	uint64_t Renderer::GetBakeRequestGeneration() const noexcept
	{
		GGLAB_ASSERT_NOT_NULL(m_EnvironmentLightingSystem.get());
		return m_EnvironmentLightingSystem->GetBakeRequestGeneration();
	}

	IBLBakeStage Renderer::GetStageForRecording() const noexcept
	{
		return m_IBLBakeScheduler ?
			m_IBLBakeScheduler->GetStageForRecording() :
			IBLBakeStage::Idle;
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

	const EnvironmentTextureSource& Renderer::GetCommittedEnvironmentSource() const noexcept
	{
		GGLAB_ASSERT_NOT_NULL(m_EnvironmentLightingSystem.get());
		return m_EnvironmentLightingSystem->GetBakeSource();
	}

	const AtmosphereGPU* Renderer::GetBakingAtmosphereParameters() const noexcept
	{
		const auto* source = m_IBLBakeScheduler->GetBakingPhysicalSky();
		return source ? &source->m_Parameters : nullptr;
	}
	Vector3 Renderer::GetBakingSunDirection() const noexcept
	{
		const auto* source = m_IBLBakeScheduler->GetBakingPhysicalSky();
		return source ? -source->m_Sun.m_Direction : Vector3::UnitY;
	}

	ArtifactCacheCoreStatistics Renderer::GetArtifactCacheStatistics() const noexcept
	{
		return m_IBLBakeScheduler ?
			m_IBLBakeScheduler->GetArtifactCacheStatistics() :
			ArtifactCacheCoreStatistics{};
	}

	LocalDerivedDataStoreStatistics Renderer::GetDerivedDataStoreStatistics() const noexcept
	{
		return m_IBLBakeScheduler ?
			m_IBLBakeScheduler->GetDerivedDataStoreStatistics() :
			LocalDerivedDataStoreStatistics{};
	}

	TemporalHistoryManagerDiagnostics Renderer::GetTemporalHistoryDiagnostics() const
	{
		return m_TemporalHistoryManager ?
			m_TemporalHistoryManager->GetDiagnostics() :
			TemporalHistoryManagerDiagnostics{};
	}

	RHIBindingLayoutDesc Renderer::GetCommonBindingLayoutDesc() const noexcept
	{
		return BuildCommonRHIBindingLayoutDesc();
	}

	RHIBindingLayoutDesc Renderer::BuildCommonRHIBindingLayoutDesc() noexcept
	{
		return scene_bindings::GetCommonLayoutContract().BuildRHIDesc("RendererCommonBindingLayout");
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
		CheckServiceLifecycleAccess();
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
		CheckServiceLifecycleAccess();
		m_IsSuspended.store(true, std::memory_order_relaxed);
	}

	void Renderer::OnResume() noexcept
	{
		CheckServiceLifecycleAccess();
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
		m_CommonBindingLayout = ResolvedRenderLayout::Create(m_RHIContext->GetPipelineSystem(),
			scene_bindings::GetCommonLayoutContract(), "RendererCommonBindingLayout");
		GGLAB_ASSERT_MSG(m_CommonBindingLayout.IsValid(),
			"Renderer failed to create the common RHI binding layout.");
	}

	void Renderer::InitializeGpuBuffers() noexcept
	{
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

	ShadowPreviewControlBase* Renderer::GetShadowPreviewControl() const noexcept
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

	FrameCaptureControlBase* Renderer::GetFrameCaptureControl() const noexcept
	{
		return m_FrameCapture.get();
	}

	RenderFrameCaptureAccess* Renderer::GetFrameCaptureAccess() const noexcept
	{
		return m_FrameCapture.get();
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
