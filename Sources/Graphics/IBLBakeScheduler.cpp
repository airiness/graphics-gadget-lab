#include "Core/Precompiled.h"
#include "Graphics/IBLBakeScheduler.h"
#include "Graphics/EnvironmentLightingSystem.h"
#include "Graphics/Profiling/GpuProfiler.h"
#include "Graphics/Resource/RenderResourceRegistry.h"
#include "Graphics/RHI/RHIDevice.h"
#include "Graphics/TransferManager.h"
#include "Core/Task/TaskSystem.h"

namespace gglab
{
	IBLBakeScheduler::IBLBakeScheduler(const CreateInfo& createInfo) noexcept :
		m_Device(createInfo.m_Device),
		m_TaskSystem(createInfo.m_TaskSystem),
		m_EnvironmentLightingSystem(createInfo.m_EnvironmentLightingSystem),
		m_RenderResourceRegistry(createInfo.m_RenderResourceRegistry),
		m_TransferManager(createInfo.m_TransferManager),
		m_GpuProfiler(createInfo.m_GpuProfiler),
		m_Cache(createInfo.m_CacheDirectory)
	{
		GGLAB_ASSERT_NOT_NULL(m_Device);
		GGLAB_ASSERT_NOT_NULL(m_TaskSystem);
		GGLAB_ASSERT_NOT_NULL(m_EnvironmentLightingSystem);
		GGLAB_ASSERT_NOT_NULL(m_RenderResourceRegistry);
		GGLAB_ASSERT_NOT_NULL(m_TransferManager);
	}

	IBLBakeScheduler::~IBLBakeScheduler()
	{
		for (auto& pending : m_PendingCacheWrites)
		{
			GGLAB_UNUSED(pending.m_Result.get());
			for (const auto& request : pending.m_Work->m_Requests)
			{
				m_TransferManager->UnmapTextureReadback(*m_Device, request);
			}
		}
	}

	void IBLBakeScheduler::Tick(const RHIFencePoint& lastSubmittedFence) noexcept
	{
		PollCacheWrites();
		m_Status.m_RequestedGeneration = m_EnvironmentLightingSystem->GetBakeRequestGeneration();

		if (m_CompletedCacheLookup)
		{
			auto work = std::move(m_CompletedCacheLookup);
			BeginBakeResourceInitialization(lastSubmittedFence, work);
		}

		if (m_InFlightFence.IsValid())
		{
			if (!m_Device->IsFencePointCompleted(m_InFlightFence))
			{
				return;
			}

			m_InFlightFence = {};
			if (m_BakeResourceInitializationInFlight)
			{
				m_BakeResourceInitializationInFlight = false;
				ContinueRequestedBakeAfterInitialization();
				return;
			}
			else if (m_CacheReadbackInFlight)
			{
				m_CacheReadbackInFlight = false;
				StartCacheWrite();
			}
			else if (m_CacheUploadInFlight)
			{
				m_CacheUploadInFlight = false;
				if (m_Status.m_BakingGeneration == m_Status.m_RequestedGeneration)
				{
					PublishBake(true);
				}
			}
			else if (m_CompletedStage != IBLBakeStage::Idle)
			{
				CaptureGpuTime(m_CompletedStage);
				if (m_Status.m_BakingGeneration == m_Status.m_RequestedGeneration)
				{
					AdvanceCompletedStage();
				}
				m_CompletedStage = IBLBakeStage::Idle;
			}
		}

		if (m_Status.m_RequestedGeneration != m_Status.m_BakingGeneration &&
			m_Status.m_RequestedGeneration != m_Status.m_ActiveGeneration)
		{
			StartRequestedBake(lastSubmittedFence);
		}
	}

	void IBLBakeScheduler::StartRequestedBake(const RHIFencePoint& retireFence) noexcept
	{
		GGLAB_UNUSED(retireFence);
		if (m_CacheLookupTask.IsValid())
		{
			GGLAB_UNUSED(m_TaskSystem->Cancel(m_CacheLookupTask));
		}
		m_CacheLookupTask = {};
		m_CompletedCacheLookup.reset();
		m_CurrentCacheLoad.reset();
		m_Status.m_BakingGeneration = m_Status.m_RequestedGeneration;
		m_Status.m_CacheHit = false;
		m_Status.m_CacheWritePending = false;
		m_Status.m_GpuMilliseconds = 0.0;
		m_Status.m_GpuTimingAvailable = false;
		m_BakingConfig = m_EnvironmentLightingSystem->GetBakeConfig();

		const EnvironmentTextureSource source = m_EnvironmentLightingSystem->GetBakeSource();
		if (!source.IsValid())
		{
			SetStage(IBLBakeStage::Failed, 0.0f);
			return;
		}
		const std::filesystem::path environmentPath = source.m_SourcePath;
		const uint64_t generation = m_Status.m_BakingGeneration;
		const bool ignoreCache = m_EnvironmentLightingSystem->ShouldIgnoreCache(generation);
		auto work = std::make_shared<CacheLoadWork>();
		work->m_Generation = generation;
		SetStage(IBLBakeStage::LoadingCache, 0.0f);
		m_CacheLookupTask = m_TaskSystem->Submit(
			{
				.m_Name = std::format("IBL.CacheRead: generation {}", generation),
				.m_Priority = TaskPriority::High,
			},
			[this, environmentPath, config = m_BakingConfig, ignoreCache, work](
				std::stop_token stopToken) noexcept
			{
				work->m_Key = m_Cache.ComputeKey(environmentPath, config, stopToken);
				if (stopToken.stop_requested() || ignoreCache)
				{
					return TaskResult::Success();
				}
				work->m_CacheHit = m_Cache.TryLoad(
					work->m_Key,
					config,
					work->m_Payload,
					stopToken);
				return TaskResult::Success();
			},
			[this, work](const TaskCompletionInfo& completion) noexcept
			{
				CompleteCacheLookup(completion, work);
			});
		if (!m_CacheLookupTask.IsValid())
		{
			SetStage(IBLBakeStage::Failed, 0.0f);
		}
	}

	void IBLBakeScheduler::CompleteCacheLookup(
		const TaskCompletionInfo& completion,
		const std::shared_ptr<CacheLoadWork>& work) noexcept
	{
		if (completion.m_Handle == m_CacheLookupTask)
		{
			m_CacheLookupTask = {};
		}
		if (work->m_Generation != m_Status.m_BakingGeneration ||
			work->m_Generation != m_Status.m_RequestedGeneration)
		{
			return;
		}
		if (completion.m_Status != TaskStatus::Succeeded)
		{
			if (completion.m_Status == TaskStatus::Cancelled)
			{
				GGLAB_LOG_GRAPHICS_INFO(
					"IBL cache lookup generation {} was cancelled.",
					work->m_Generation);
			}
			else
			{
				GGLAB_LOG_GRAPHICS_ERROR(
					"IBL cache lookup for generation {} failed: {}",
					work->m_Generation,
					completion.m_Error);
			}
			SetStage(IBLBakeStage::Failed, 0.0f);
			return;
		}
		GGLAB_LOG_GRAPHICS_INFO(
			"IBL cache lookup generation {} completed (cache={}, key={:016x}, queueMs={:.2f}, cpuMs={:.2f}).",
			work->m_Generation,
			work->m_CacheHit ? "hit" : "miss",
			work->m_Key,
			completion.m_QueueMilliseconds,
			completion.m_ExecutionMilliseconds);
		m_CompletedCacheLookup = work;
	}

	void IBLBakeScheduler::BeginBakeResourceInitialization(
		const RHIFencePoint& retireFence,
		const std::shared_ptr<CacheLoadWork>& work) noexcept
	{
		if (work->m_Generation != m_Status.m_BakingGeneration ||
			work->m_Generation != m_Status.m_RequestedGeneration)
		{
			return;
		}

		m_Status.m_CacheKey = work->m_Key;
		m_CurrentCacheLoad = work;
		const RHIFencePoint* retireFencePtr = retireFence.IsValid() ? &retireFence : nullptr;
		m_RenderResourceRegistry->EnsureIBLBakeResources(m_BakingConfig, retireFencePtr);
		if (!m_RenderResourceRegistry->HasIBLBakeResources())
		{
			m_BakeResourcesNeedInitialization = false;
			m_CurrentCacheLoad.reset();
			SetStage(IBLBakeStage::Failed, 0.0f);
			return;
		}

		// Bake targets are allocated as render-target textures from heaps created
		// with CREATE_NOT_ZEROED. They remain live across multiple bake stages, so
		// initialize every subresource before PIX or another tool attempts to
		// preserve their contents with a copy operation.
		m_BakeResourcesNeedInitialization = true;
		m_BakeResourceInitializationExecuted = false;
		SetStage(IBLBakeStage::LoadingCache, 0.05f);
	}

	void IBLBakeScheduler::ContinueRequestedBakeAfterInitialization() noexcept
	{
		if (m_Status.m_BakingGeneration != m_Status.m_RequestedGeneration)
		{
			return;
		}

		auto cacheLoad = std::move(m_CurrentCacheLoad);
		if (cacheLoad && cacheLoad->m_CacheHit &&
			UploadCachePayload(cacheLoad->m_Payload))
		{
			m_Status.m_CacheHit = true;
			m_CacheUploadInFlight = true;
			SetStage(IBLBakeStage::WaitingForGpu, 0.95f);
			return;
		}
		if (!m_EnvironmentLightingSystem->GetBakeSource().IsValid())
		{
			SetStage(IBLBakeStage::Failed, 0.0f);
			return;
		}

		SetStage(IBLBakeStage::Environment, 0.0f);
	}

	bool IBLBakeScheduler::UploadCachePayload(const IBLBakeCachePayload& payload) noexcept
	{
		using TextureIndex = RenderResourceRegistry::TextureIndex;
		TransferBatch batch = m_TransferManager->BeginBatch();
		const bool uploaded =
			batch.UploadTexture(
				m_RenderResourceRegistry->GetIBLBakeTextureHandle(TextureIndex::IBL_EnvironmentCubemap),
				payload.m_Environment.MakeUploadData()) &&
			batch.UploadTexture(
				m_RenderResourceRegistry->GetIBLBakeTextureHandle(TextureIndex::IBL_IrradianceCubemap),
				payload.m_Irradiance.MakeUploadData()) &&
			batch.UploadTexture(
				m_RenderResourceRegistry->GetIBLBakeTextureHandle(TextureIndex::IBL_PrefilteredSpecularCubemap),
				payload.m_PrefilteredSpecular.MakeUploadData()) &&
			batch.UploadTexture(
				m_RenderResourceRegistry->GetIBLBakeTextureHandle(TextureIndex::IBL_BrdfLut),
				payload.m_BrdfLut.MakeUploadData());

		std::array<RHITextureBarrier, 4> barriers{};
		const TextureIndex indices[] = {
			TextureIndex::IBL_EnvironmentCubemap,
			TextureIndex::IBL_IrradianceCubemap,
			TextureIndex::IBL_PrefilteredSpecularCubemap,
			TextureIndex::IBL_BrdfLut,
		};
		for (size_t index = 0; index < barriers.size(); ++index)
		{
			barriers[index] = {
				.m_Texture = m_RenderResourceRegistry->GetIBLBakeTextureHandle(indices[index]),
				.m_Before = {
					.m_Stages = RHIStage::Copy,
					.m_Access = RHIAccess::CopyDest,
					.m_Layout = RHILayout::CopyDest,
				},
				.m_After = {
					.m_Stages = RHIStage::All,
					.m_Access = RHIAccess::Common,
					.m_Layout = RHILayout::Common,
				},
			};
		}
		batch.TextureBarrier(barriers);
		m_InFlightFence = batch.Submit(false);
		return uploaded && m_InFlightFence.IsValid();
	}

	void IBLBakeScheduler::NotifyStageExecuted(IBLBakeStage stage, uint64_t generation) noexcept
	{
		if (!IsGpuStage(stage) || stage != m_Status.m_Stage || generation != m_Status.m_BakingGeneration)
		{
			return;
		}
		m_ExecutedStage = stage;
	}

	void IBLBakeScheduler::NotifyBakeResourcesInitialized(uint64_t generation) noexcept
	{
		if (!m_BakeResourcesNeedInitialization || generation != m_Status.m_BakingGeneration)
		{
			return;
		}
		m_BakeResourceInitializationExecuted = true;
	}

	void IBLBakeScheduler::OnFrameSubmitted(const RHIFencePoint& fencePoint) noexcept
	{
		if (m_BakeResourceInitializationExecuted)
		{
			m_BakeResourcesNeedInitialization = false;
			m_BakeResourceInitializationExecuted = false;
			m_BakeResourceInitializationInFlight = true;
			GGLAB_ASSERT_MSG(fencePoint.IsValid(),
				"Submitted IBL bake resource initialization requires a valid frame fence.");
			m_InFlightFence = fencePoint;
		}
		if (m_ExecutedStage == IBLBakeStage::Idle)
		{
			return;
		}
		GGLAB_ASSERT_MSG(fencePoint.IsValid(), "A recorded IBL bake stage requires a valid frame fence.");
		m_InFlightFence = fencePoint;
		m_CompletedStage = m_ExecutedStage;
		m_ExecutedStage = IBLBakeStage::Idle;
		SetStage(IBLBakeStage::WaitingForGpu, m_Status.m_Progress);
	}

	void IBLBakeScheduler::OnFrameAborted() noexcept
	{
		m_ExecutedStage = IBLBakeStage::Idle;
		m_BakeResourceInitializationExecuted = false;
	}

	IBLBakeStage IBLBakeScheduler::GetStageForRecording() const noexcept
	{
		return !m_InFlightFence.IsValid() && IsGpuStage(m_Status.m_Stage) ?
			m_Status.m_Stage : IBLBakeStage::Idle;
	}

	void IBLBakeScheduler::AdvanceCompletedStage() noexcept
	{
		switch (m_CompletedStage)
		{
		case IBLBakeStage::Environment:
			SetStage(
				m_BakingConfig.m_EnvironmentCubemapSize > 1 ?
					IBLBakeStage::EnvironmentMipChain : IBLBakeStage::Irradiance,
				0.2f);
			break;
		case IBLBakeStage::EnvironmentMipChain:
			SetStage(IBLBakeStage::Irradiance, 0.4f);
			break;
		case IBLBakeStage::Irradiance:
			SetStage(IBLBakeStage::PrefilteredSpecular, 0.6f);
			break;
		case IBLBakeStage::PrefilteredSpecular:
			SetStage(IBLBakeStage::BrdfLut, 0.8f);
			break;
		case IBLBakeStage::BrdfLut:
			SetStage(IBLBakeStage::SavingCache, 0.95f);
			StartCacheReadback();
			PublishBake(false);
			break;
		default:
			break;
		}
	}

	bool IBLBakeScheduler::StartCacheReadback() noexcept
	{
		using TextureIndex = RenderResourceRegistry::TextureIndex;
		constexpr std::array indices = {
			TextureIndex::IBL_EnvironmentCubemap, TextureIndex::IBL_IrradianceCubemap,
			TextureIndex::IBL_PrefilteredSpecularCubemap, TextureIndex::IBL_BrdfLut };
		auto work = std::make_shared<CacheWriteWork>();
		work->m_Key = m_Status.m_CacheKey;
		TransferBatch batch = m_TransferManager->BeginBatch();
		for (size_t index = 0; index < indices.size(); ++index)
		{
			const auto* desc = m_RenderResourceRegistry->GetIBLBakeTextureDesc(indices[index]);
			if (!desc)
			{
				return false;
			}
			work->m_Requests[index] = batch.ReadbackTexture(
				m_RenderResourceRegistry->GetIBLBakeTextureHandle(indices[index]), *desc);
			if (!work->m_Requests[index].IsValid())
			{
				return false;
			}
		}
		m_InFlightFence = batch.Submit(false);
		m_CacheReadbackInFlight = m_InFlightFence.IsValid();
		m_ReadbackWork = m_CacheReadbackInFlight ? std::move(work) : nullptr;
		m_Status.m_CacheWritePending = m_CacheReadbackInFlight || !m_PendingCacheWrites.empty();
		return m_CacheReadbackInFlight;
	}

	void IBLBakeScheduler::StartCacheWrite() noexcept
	{
		auto work = std::move(m_ReadbackWork);
		if (!work)
		{
			return;
		}
		for (size_t index = 0; index < work->m_Requests.size(); ++index)
		{
			work->m_Mapped[index] = m_TransferManager->MapTextureReadback(*m_Device, work->m_Requests[index]);
			if (!work->m_Mapped[index])
			{
				for (size_t mappedIndex = 0; mappedIndex < index; ++mappedIndex)
				{
					m_TransferManager->UnmapTextureReadback(*m_Device, work->m_Requests[mappedIndex]);
				}
				return;
			}
		}
		try
		{
			m_PendingCacheWrites.push_back({ work, std::async(std::launch::async, [this, work]() noexcept
				{
					IBLBakeCachePayload payload{};
					TextureAssetData* outputs[] = { &payload.m_Environment, &payload.m_Irradiance,
						&payload.m_PrefilteredSpecular, &payload.m_BrdfLut };
					for (size_t index = 0; index < work->m_Requests.size(); ++index)
					{
						*outputs[index] = TransferManager::ResolveMappedTextureReadback(
							work->m_Requests[index], work->m_Mapped[index]);
						if (!outputs[index]->IsValid()) return false;
					}
					return m_Cache.Store(work->m_Key, payload);
				}) });
		}
		catch (...)
		{
			for (const auto& request : work->m_Requests)
			{
				m_TransferManager->UnmapTextureReadback(*m_Device, request);
			}
			GGLAB_LOG_GRAPHICS_WARN("Failed to start the asynchronous IBL cache writer.");
		}
	}

	void IBLBakeScheduler::PollCacheWrites() noexcept
	{
		for (auto iterator = m_PendingCacheWrites.begin(); iterator != m_PendingCacheWrites.end();)
		{
			if (iterator->m_Result.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
			{
				++iterator;
				continue;
			}
			const bool stored = iterator->m_Result.get();
			for (const auto& request : iterator->m_Work->m_Requests)
			{
				m_TransferManager->UnmapTextureReadback(*m_Device, request);
			}
			if (!stored)
			{
				GGLAB_LOG_GRAPHICS_WARN("IBL bake {:016x} completed, but its persistent cache could not be written.",
					iterator->m_Work->m_Key);
			}
			iterator = m_PendingCacheWrites.erase(iterator);
		}
		m_Status.m_CacheWritePending = m_CacheReadbackInFlight || !m_PendingCacheWrites.empty();
	}

	void IBLBakeScheduler::PublishBake(bool cacheHit) noexcept
	{
		m_RenderResourceRegistry->PublishIBLBakeResources();
		m_Status.m_ActiveGeneration = m_Status.m_BakingGeneration;
		m_Status.m_CacheHit = cacheHit;
		m_Status.m_HasActiveBake = true;
		SetStage(IBLBakeStage::Ready, 1.0f);
		GGLAB_LOG_GRAPHICS_INFO(
			"IBL bake generation {} published (cache={}, key={:016x}, gpuMs={:.3f}).",
			m_Status.m_ActiveGeneration,
			cacheHit ? "hit" : "miss",
			m_Status.m_CacheKey,
			m_Status.m_GpuMilliseconds);
	}

	void IBLBakeScheduler::CaptureGpuTime(IBLBakeStage stage) noexcept
	{
		if (!m_GpuProfiler || !m_GpuProfiler->IsEnabled())
		{
			return;
		}
		const auto snapshot = m_GpuProfiler->GetLatestFrame();
		if (!snapshot.IsValid())
		{
			return;
		}

		std::string_view match;
		switch (stage)
		{
		case IBLBakeStage::Environment: match = "IBL.Environment"; break;
		case IBLBakeStage::EnvironmentMipChain: match = "IBL.EnvironmentMipChain"; break;
		case IBLBakeStage::Irradiance: match = "IBL.Irradiance"; break;
		case IBLBakeStage::PrefilteredSpecular: match = "IBL.PrefilteredSpecular"; break;
		case IBLBakeStage::BrdfLut: match = "IBL.BrdfLUT"; break;
		default: return;
		}
		for (const auto& sample : snapshot.m_Samples)
		{
			if (sample.m_Name.starts_with(match))
			{
				m_Status.m_GpuMilliseconds += sample.m_Milliseconds;
				m_Status.m_GpuTimingAvailable = true;
			}
		}
	}

	void IBLBakeScheduler::SetStage(IBLBakeStage stage, float progress) noexcept
	{
		m_Status.m_Stage = stage;
		m_Status.m_Progress = std::clamp(progress, 0.0f, 1.0f);
	}

	bool IBLBakeScheduler::IsGpuStage(IBLBakeStage stage) const noexcept
	{
		return stage == IBLBakeStage::Environment ||
			stage == IBLBakeStage::EnvironmentMipChain ||
			stage == IBLBakeStage::Irradiance ||
			stage == IBLBakeStage::PrefilteredSpecular ||
			stage == IBLBakeStage::BrdfLut;
	}
}
