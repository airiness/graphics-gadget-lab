#include "Graphics/IBLBakeScheduler.h"
#include "GGLabFoundation/Base/CoreMacros.h"
#include "Core/Log/LogMacros.h"
#include "Core/Task/TaskSystem.h"
#include "Graphics/Asset/AssetManager.h"
#include "Graphics/EnvironmentLightingSystem.h"
#include "Graphics/Profiling/GpuProfiler.h"
#include "Graphics/Resource/RenderResourceRegistry.h"
#include "Graphics/RHI/RHIDevice.h"
#include "Graphics/TransferManager.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <format>
#include <memory>
#include <stop_token>
#include <string>
#include <string_view>
#include <utility>

namespace gglab
{
	namespace
	{
		using TextureIndex = RenderResourceRegistry::TextureIndex;

		constexpr std::array<IBLArtifactStage, static_cast<size_t>(IBLArtifactStage::Count)>
			ArtifactStages = {
				IBLArtifactStage::Environment,
				IBLArtifactStage::Irradiance,
				IBLArtifactStage::PrefilteredSpecular,
				IBLArtifactStage::BrdfLut,
		};

		constexpr std::array<TextureIndex, static_cast<size_t>(IBLArtifactStage::Count)>
			ArtifactTextureIndices = {
				TextureIndex::IBL_EnvironmentCubemap,
				TextureIndex::IBL_IrradianceCubemap,
				TextureIndex::IBL_PrefilteredSpecularCubemap,
				TextureIndex::IBL_BrdfLut,
		};

		[[nodiscard]] consteval bool ValidateResourceInitializationAbortSupersede() noexcept
		{
			detail::IBLBakeResourceInitializationState state;
			if (!state.Begin(1) ||
				!state.ShouldRecord(1) || state.ShouldRecord(2) ||
				!state.NotifyExecuted(1))
			{
				return false;
			}

			state.AbortFrame();
			if (!state.ShouldRecord(1) ||
				!state.ResetForRequestedBake() ||
				state.ShouldRecord(1) || state.NotifyExecuted(2))
			{
				return false;
			}

			if (!state.Begin(2) ||
				!state.ShouldRecord(2) || !state.NotifyExecuted(2) ||
				!state.Submit() || !state.IsInFlight() || state.ResetForRequestedBake())
			{
				return false;
			}
			return state.Complete() == 2 && !state.IsInFlight() && !state.ShouldRecord(2);
		}

		static_assert(ValidateResourceInitializationAbortSupersede(),
			"IBL resource initialization must not survive an abort and superseding generation.");
	}

	IBLBakeScheduler::IBLBakeScheduler(const CreateInfo& createInfo) noexcept :
		m_Device(createInfo.m_Device), m_TaskSystem(createInfo.m_TaskSystem),
		m_EnvironmentLightingSystem(createInfo.m_EnvironmentLightingSystem),
		m_RenderResourceRegistry(createInfo.m_RenderResourceRegistry),
		m_TransferManager(createInfo.m_TransferManager), m_GpuProfiler(createInfo.m_GpuProfiler),
		m_DerivedDataSystem({
			.m_CacheDirectory = createInfo.m_DerivedDataCacheDirectory,
			.m_ShaderSourceRoot = createInfo.m_ShaderSourceRoot,
			.m_ArtifactCache = createInfo.m_ArtifactCache,
			.m_Compatibility = IBLArtifactCompatibility::AdapterScoped,
			.m_AdapterScopeIdentity = std::string(
				createInfo.m_Device ? createInfo.m_Device->GetAdapterCompatibilityIdentity()
									: std::string_view{}),
			})
	{
		GGLAB_ASSERT_NOT_NULL(m_Device);
		GGLAB_ASSERT_NOT_NULL(m_TaskSystem);
		GGLAB_ASSERT_NOT_NULL(m_EnvironmentLightingSystem);
		GGLAB_ASSERT_NOT_NULL(m_RenderResourceRegistry);
		GGLAB_ASSERT_NOT_NULL(m_TransferManager);
	}

	IBLBakeScheduler::~IBLBakeScheduler()
	{
		GGLAB_ASSERT_MSG(
			!m_BakingSourceOwner, "IBLBakeScheduler destroyed before detaching AssetManager.");
		GGLAB_ASSERT_MSG(!m_CacheLookupTask.IsValid() && m_PendingCacheWrites.empty(),
			"IBLBakeScheduler destroyed before its CPU cache/DDC tasks completed.");
	}

	void IBLBakeScheduler::AttachAssetManager(AssetManager& assetManager) noexcept
	{
		GGLAB_ASSERT_MSG(
			!m_BakingSourceOwner, "IBLBakeScheduler already has an attached AssetManager.");
		if (m_BakingSourceOwner)
		{
			return;
		}
		m_BakingSourceOwner = std::make_unique<AssetOwnerScope>(assetManager.CreateOwnerScope());
	}

	void IBLBakeScheduler::DetachAssetManager() noexcept
	{
		ReleaseBakingSourceLease();
		m_BakingSourceOwner.reset();
	}

	void IBLBakeScheduler::Tick(const RHIFencePoint& lastSubmittedFence) noexcept
	{
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
			if (m_BakeResourceInitialization.IsInFlight())
			{
				const uint64_t generation = m_BakeResourceInitialization.Complete();
				ContinueRequestedBakeAfterInitialization(generation);
				return;
			}
			if (m_CacheReadbackInFlight)
			{
				m_CacheReadbackInFlight = false;
				StartCacheWrite();
			}
			else if (m_CacheUploadInFlight)
			{
				m_CacheUploadInFlight = false;
				if (m_Status.m_BakingGeneration == m_Status.m_RequestedGeneration)
				{
					if (m_Status.m_Artifacts[static_cast<size_t>(IBLArtifactStage::Environment)]
						.m_Resolution != IBLArtifactResolution::Miss)
					{
						ReleaseBakingSourceLease();
					}
					AdvanceToNextMissingStage();
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
		// A frame abort keeps initialization recordable for the same generation.
		// A superseding bake must discard that state before adopting new resources.
		const bool resetInitialization = m_BakeResourceInitialization.ResetForRequestedBake();
		GGLAB_ASSERT_MSG(resetInitialization,
			"An in-flight IBL resource initialization must complete before superseding its bake.");
		if (!resetInitialization)
		{
			return;
		}
		ReleaseBakingSourceLease();
		if (m_CacheLookupTask.IsValid())
		{
			GGLAB_UNUSED(m_TaskSystem->Cancel(m_CacheLookupTask));
		}
		m_CacheLookupTask = {};
		m_CompletedCacheLookup.reset();
		m_CurrentCacheLoad.reset();
		m_Status.m_BakingGeneration = m_Status.m_RequestedGeneration;
		m_Status.m_Artifacts = {};
		m_Status.m_CacheHit = false;
		m_Status.m_PartialCacheHit = false;
		m_Status.m_CpuCacheHit = false;
		m_Status.m_DerivedDataCacheHit = false;
		m_Status.m_CacheHitStageCount = 0;
		m_Status.m_GpuBuildStageCount = 0;
		m_Status.m_CacheWritePending = m_CacheReadbackInFlight || !m_PendingCacheWrites.empty();
		m_Status.m_GpuMilliseconds = 0.0;
		m_Status.m_GpuTimingAvailable = false;
		m_BakingRequest = {
			.m_Generation = m_Status.m_BakingGeneration,
			.m_Source = m_EnvironmentLightingSystem->GetBakeSource(),
			.m_Config = m_EnvironmentLightingSystem->GetBakeConfig(),
			.m_IgnoreCache =
				m_EnvironmentLightingSystem->ShouldIgnoreCache(m_Status.m_BakingGeneration),
		};

		if (!m_BakingRequest.IsValid() || !m_BakingSourceOwner ||
			!m_BakingSourceOwner->RetainTexture(
				m_BakingRequest.m_Source.m_Content, TaskPriority::High))
		{
			ReleaseBakingSourceLease();
			SetStage(IBLBakeStage::Failed, 0.0f);
			return;
		}

		const AssetContentFingerprint contentFingerprint =
			m_BakingRequest.m_Source.m_ContentFingerprint;
		const EnvironmentTextureSourceType sourceType = m_BakingRequest.m_Source.m_Type;
		const uint64_t generation = m_Status.m_BakingGeneration;
		const bool ignoreCache = m_BakingRequest.m_IgnoreCache;
		auto work = std::make_shared<CacheLoadWork>();
		work->m_Generation = generation;
		SetStage(IBLBakeStage::LoadingCache, 0.0f);
		m_CacheLookupTask = m_TaskSystem->Submit(
			{
				.m_Name = std::format("IBL.StageCacheRead: generation {}", generation),
				.m_Priority = TaskPriority::High,
			},
			[this, contentFingerprint, sourceType, config = m_BakingRequest.m_Config, ignoreCache,
			work](std::stop_token stopToken) noexcept
			{
				work->m_Result = m_DerivedDataSystem.Lookup(
					contentFingerprint, sourceType, config, ignoreCache, stopToken);
				return TaskResult::Success();
			},
			[this, work](const TaskCompletionInfo& completion) noexcept
			{ CompleteCacheLookup(completion, work); });
		if (!m_CacheLookupTask.IsValid())
		{
			ReleaseBakingSourceLease();
			SetStage(IBLBakeStage::Failed, 0.0f);
		}
	}

	void IBLBakeScheduler::CompleteCacheLookup(
		const TaskCompletionInfo& completion, const std::shared_ptr<CacheLoadWork>& work) noexcept
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
			ReleaseBakingSourceLease();
			if (completion.m_Status == TaskStatus::Cancelled)
			{
				GGLAB_LOG_GRAPHICS_INFO(
					"IBL stage cache lookup generation {} was cancelled.", work->m_Generation);
			}
			else
			{
				GGLAB_LOG_GRAPHICS_ERROR("IBL stage cache lookup for generation {} failed: {}",
					work->m_Generation, completion.m_Error);
			}
			SetStage(IBLBakeStage::Failed, 0.0f);
			return;
		}

		uint32_t cpuHits = 0;
		uint32_t ddcHits = 0;
		for (IBLArtifactStage stage : ArtifactStages)
		{
			const auto& stageResult = work->m_Result.Get(stage);
			cpuHits += stageResult.m_Source == IBLDerivedDataSource::CpuCache ? 1u : 0u;
			ddcHits += stageResult.m_Source == IBLDerivedDataSource::LocalDdc ? 1u : 0u;
			if (!stageResult.m_Error.empty())
			{
				GGLAB_LOG_GRAPHICS_WARN("IBL {} cache entry was ignored: {}",
					GetIBLArtifactStageName(stage), stageResult.m_Error);
			}
		}
		GGLAB_LOG_GRAPHICS_INFO(
			"IBL stage cache lookup generation {} completed (cpuHits={}, ddcHits={}, misses={}, queueMs={:.2f}, cpuMs={:.2f}).",
			work->m_Generation, cpuHits, ddcHits,
			static_cast<uint32_t>(IBLArtifactStage::Count) - cpuHits - ddcHits,
			completion.m_QueueMilliseconds, completion.m_ExecutionMilliseconds);
		m_CompletedCacheLookup = work;
	}

	void IBLBakeScheduler::BeginBakeResourceInitialization(
		const RHIFencePoint& retireFence, const std::shared_ptr<CacheLoadWork>& work) noexcept
	{
		if (work->m_Generation != m_Status.m_BakingGeneration ||
			work->m_Generation != m_Status.m_RequestedGeneration)
		{
			return;
		}

		for (IBLArtifactStage stage : ArtifactStages)
		{
			const auto& result = work->m_Result.Get(stage);
			auto& status = m_Status.m_Artifacts[static_cast<size_t>(stage)];
			status.m_DerivedDataKey = result.m_Key;
			status.m_ContentDigest =
				result.m_Artifact ? result.m_Artifact->m_ContentDigest : ArtifactContentDigest{};
			switch (result.m_Source)
			{
			case IBLDerivedDataSource::CpuCache:
				status.m_Resolution = IBLArtifactResolution::CpuCache;
				m_Status.m_CpuCacheHit = true;
				++m_Status.m_CacheHitStageCount;
				break;
			case IBLDerivedDataSource::LocalDdc:
				status.m_Resolution = IBLArtifactResolution::LocalDdc;
				m_Status.m_DerivedDataCacheHit = true;
				++m_Status.m_CacheHitStageCount;
				break;
			case IBLDerivedDataSource::Miss:
				status.m_Resolution = IBLArtifactResolution::Miss;
				break;
			}
		}
		m_Status.m_CacheHit =
			m_Status.m_CacheHitStageCount == static_cast<uint32_t>(IBLArtifactStage::Count);
		m_Status.m_PartialCacheHit = m_Status.m_CacheHitStageCount > 0 && !m_Status.m_CacheHit;

		m_CurrentCacheLoad = work;
		const RHIFencePoint* retireFencePtr = retireFence.IsValid() ? &retireFence : nullptr;
		m_RenderResourceRegistry->EnsureIBLBakeResources(m_BakingRequest.m_Config, retireFencePtr);
		if (!m_RenderResourceRegistry->HasIBLBakeResources())
		{
			ReleaseBakingSourceLease();
			m_CurrentCacheLoad.reset();
			SetStage(IBLBakeStage::Failed, 0.0f);
			return;
		}

		// Staging targets come from CREATE_NOT_ZEROED heaps and remain live across
		// several passes. Initialize every subresource before uploading hits or
		// recording any missing stage.
		if (!m_BakeResourceInitialization.Begin(work->m_Generation))
		{
			ReleaseBakingSourceLease();
			m_CurrentCacheLoad.reset();
			SetStage(IBLBakeStage::Failed, 0.0f);
			return;
		}
		SetStage(IBLBakeStage::LoadingCache, 0.05f);
	}

	void IBLBakeScheduler::ContinueRequestedBakeAfterInitialization(uint64_t generation) noexcept
	{
		auto cacheLoad = std::move(m_CurrentCacheLoad);
		if (generation == 0 || generation != m_Status.m_BakingGeneration ||
			generation != m_Status.m_RequestedGeneration)
		{
			return;
		}

		if (!cacheLoad || cacheLoad->m_Generation != generation)
		{
			ReleaseBakingSourceLease();
			SetStage(IBLBakeStage::Failed, 0.0f);
			return;
		}
		if (m_Status.m_CacheHitStageCount > 0)
		{
			if (!UploadCachedArtifacts(cacheLoad->m_Result))
			{
				ReleaseBakingSourceLease();
				SetStage(IBLBakeStage::Failed, 0.0f);
				return;
			}
			m_CacheUploadInFlight = true;
			SetStage(IBLBakeStage::WaitingForGpu, 0.1f);
			return;
		}

		AdvanceToNextMissingStage();
	}

	bool IBLBakeScheduler::UploadCachedArtifacts(const IBLDerivedDataLookupResult& result) noexcept
	{
		TransferBatch batch = m_TransferManager->BeginBatch();
		bool uploaded = true;
		uint32_t uploadCount = 0;
		for (IBLArtifactStage stage : ArtifactStages)
		{
			const IBLStageArtifactHandle& artifact = result.Get(stage).m_Artifact;
			if (!artifact)
			{
				continue;
			}
			const TextureIndex textureIndex = ArtifactTextureIndices[static_cast<size_t>(stage)];
			const RHITextureHandle texture =
				m_RenderResourceRegistry->GetIBLBakeTextureHandle(textureIndex);
			const RHIResourceState commonState{
				.m_Stages = RHIStage::All,
				.m_Access = RHIAccess::Common,
				.m_Layout = RHILayout::Common,
			};
			uploaded &= batch.UploadTexture(texture, artifact->m_Texture.MakeUploadData(),
				UndefinedRHITextureState(), commonState);
			++uploadCount;
		}
		if (uploadCount == 0)
		{
			return false;
		}
		m_InFlightFence = batch.Submit(false).m_Completion;
		return uploaded && m_InFlightFence.IsValid();
	}

	void IBLBakeScheduler::NotifyStageExecuted(IBLBakeStage stage, uint64_t generation) noexcept
	{
		if (!IsGpuStage(stage) || stage != m_Status.m_Stage ||
			generation != m_Status.m_BakingGeneration)
		{
			return;
		}
		m_ExecutedStage = stage;
	}

	void IBLBakeScheduler::NotifyBakeResourcesInitialized(uint64_t generation) noexcept
	{
		if (generation != m_Status.m_BakingGeneration)
		{
			return;
		}
		GGLAB_UNUSED(m_BakeResourceInitialization.NotifyExecuted(generation));
	}

	void IBLBakeScheduler::OnFrameSubmitted(const RHIFencePoint& fencePoint) noexcept
	{
		if (m_BakeResourceInitialization.HasExecuted())
		{
			GGLAB_ASSERT_MSG(fencePoint.IsValid(),
				"Submitted IBL bake resource initialization requires a valid frame fence.");
			if (!fencePoint.IsValid())
			{
				m_BakeResourceInitialization.AbortFrame();
			}
			else if (m_BakeResourceInitialization.Submit())
			{
				m_InFlightFence = fencePoint;
			}
		}
		if (m_ExecutedStage == IBLBakeStage::Idle)
		{
			return;
		}
		GGLAB_ASSERT_MSG(
			fencePoint.IsValid(), "A recorded IBL bake stage requires a valid frame fence.");
		m_InFlightFence = fencePoint;
		m_CompletedStage = m_ExecutedStage;
		m_ExecutedStage = IBLBakeStage::Idle;
		SetStage(IBLBakeStage::WaitingForGpu, m_Status.m_Progress);
	}

	void IBLBakeScheduler::OnFrameAborted() noexcept
	{
		m_ExecutedStage = IBLBakeStage::Idle;
		m_BakeResourceInitialization.AbortFrame();
	}

	IBLBakeStage IBLBakeScheduler::GetStageForRecording() const noexcept
	{
		return !m_InFlightFence.IsValid() && IsGpuStage(m_Status.m_Stage) ? m_Status.m_Stage
			: IBLBakeStage::Idle;
	}

	void IBLBakeScheduler::AdvanceCompletedStage() noexcept
	{
		switch (m_CompletedStage)
		{
		case IBLBakeStage::Environment:
			ReleaseBakingSourceLease();
			if (m_BakingRequest.m_Config.m_EnvironmentCubemapSize > 1)
			{
				SetStage(IBLBakeStage::EnvironmentMipChain, 0.2f);
			}
			else
			{
				MarkGpuStageBuilt(IBLArtifactStage::Environment);
				AdvanceToNextMissingStage();
			}
			break;
		case IBLBakeStage::EnvironmentMipChain:
			MarkGpuStageBuilt(IBLArtifactStage::Environment);
			AdvanceToNextMissingStage();
			break;
		case IBLBakeStage::Irradiance:
			MarkGpuStageBuilt(IBLArtifactStage::Irradiance);
			AdvanceToNextMissingStage();
			break;
		case IBLBakeStage::PrefilteredSpecular:
			MarkGpuStageBuilt(IBLArtifactStage::PrefilteredSpecular);
			AdvanceToNextMissingStage();
			break;
		case IBLBakeStage::BrdfLut:
			MarkGpuStageBuilt(IBLArtifactStage::BrdfLut);
			AdvanceToNextMissingStage();
			break;
		default:
			break;
		}
	}

	void IBLBakeScheduler::AdvanceToNextMissingStage() noexcept
	{
		const auto isMissing = [this](IBLArtifactStage stage) noexcept
			{
				return m_Status.m_Artifacts[static_cast<size_t>(stage)].m_Resolution ==
					IBLArtifactResolution::Miss;
			};
		if (isMissing(IBLArtifactStage::Environment))
		{
			if (!m_BakingRequest.m_Source.IsValid())
			{
				ReleaseBakingSourceLease();
				SetStage(IBLBakeStage::Failed, 0.0f);
				return;
			}
			SetStage(IBLBakeStage::Environment, 0.1f);
			return;
		}
		if (isMissing(IBLArtifactStage::Irradiance))
		{
			SetStage(IBLBakeStage::Irradiance, 0.4f);
			return;
		}
		if (isMissing(IBLArtifactStage::PrefilteredSpecular))
		{
			SetStage(IBLBakeStage::PrefilteredSpecular, 0.6f);
			return;
		}
		if (isMissing(IBLArtifactStage::BrdfLut))
		{
			SetStage(IBLBakeStage::BrdfLut, 0.8f);
			return;
		}

		ReleaseBakingSourceLease();
		if (m_Status.m_GpuBuildStageCount > 0)
		{
			SetStage(IBLBakeStage::SavingCache, 0.95f);
			GGLAB_UNUSED(StartCacheReadback());
		}
		PublishBake();
	}

	void IBLBakeScheduler::MarkGpuStageBuilt(IBLArtifactStage stage) noexcept
	{
		auto& status = m_Status.m_Artifacts[static_cast<size_t>(stage)];
		GGLAB_ASSERT(status.m_Resolution == IBLArtifactResolution::Miss);
		status.m_Resolution = IBLArtifactResolution::GpuBuild;
		++m_Status.m_GpuBuildStageCount;
	}

	void IBLBakeScheduler::ReleaseBakingSourceLease() noexcept
	{
		if (m_BakingSourceOwner)
		{
			m_BakingSourceOwner->Reset();
		}
	}

	bool IBLBakeScheduler::StartCacheReadback() noexcept
	{
		auto work = std::make_shared<CacheWriteWork>();
		work->m_Generation = m_Status.m_BakingGeneration;
		work->m_Config = m_BakingRequest.m_Config;
		TransferBatch batch = m_TransferManager->BeginBatch();
		uint32_t readbackCount = 0;
		for (IBLArtifactStage stage : ArtifactStages)
		{
			const size_t index = static_cast<size_t>(stage);
			const auto& stageStatus = m_Status.m_Artifacts[index];
			work->m_Keys[index] = stageStatus.m_DerivedDataKey;
			work->m_BuiltStages[index] =
				stageStatus.m_Resolution == IBLArtifactResolution::GpuBuild;
			if (!work->m_BuiltStages[index] || !work->m_Keys[index].IsValid())
			{
				continue;
			}

			const TextureIndex textureIndex = ArtifactTextureIndices[index];
			const auto* desc = m_RenderResourceRegistry->GetIBLBakeTextureDesc(textureIndex);
			if (!desc)
			{
				return false;
			}
			work->m_Requests[index] = batch.ReadbackTexture(
				m_RenderResourceRegistry->GetIBLBakeTextureHandle(textureIndex), *desc);
			if (!work->m_Requests[index].IsValid())
			{
				return false;
			}
			++readbackCount;
		}
		if (readbackCount == 0)
		{
			return false;
		}

		m_InFlightFence = batch.Submit(false).m_Completion;
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

		uint32_t artifactCount = 0;
		for (IBLArtifactStage stage : ArtifactStages)
		{
			const size_t index = static_cast<size_t>(stage);
			if (!work->m_BuiltStages[index] || !work->m_Requests[index].IsValid())
			{
				continue;
			}
			const std::byte* mapped =
				m_TransferManager->MapTextureReadback(*m_Device, work->m_Requests[index]);
			if (!mapped)
			{
				GGLAB_LOG_GRAPHICS_WARN("IBL {} readback could not be mapped for DDC publication.",
					GetIBLArtifactStageName(stage));
				continue;
			}
			TextureAssetData texture =
				TransferManager::ResolveMappedTextureReadback(work->m_Requests[index], mapped);
			m_TransferManager->UnmapTextureReadback(*m_Device, work->m_Requests[index]);

			IBLStageArtifactHandle artifact = CreateIBLStageArtifact(stage, std::move(texture));
			if (!artifact || !artifact->MatchesConfig(work->m_Config))
			{
				GGLAB_LOG_GRAPHICS_WARN(
					"IBL {} produced an invalid stage artifact; skipping DDC publication.",
					GetIBLArtifactStageName(stage));
				continue;
			}
			artifact = m_DerivedDataSystem.Admit(work->m_Keys[index], std::move(artifact));
			if (!artifact)
			{
				GGLAB_LOG_GRAPHICS_WARN("IBL {} could not admit its immutable stage artifact.",
					GetIBLArtifactStageName(stage));
				continue;
			}
			work->m_Artifacts.Set(stage, artifact);
			if (work->m_Generation == m_Status.m_BakingGeneration &&
				work->m_Keys[index] == m_Status.m_Artifacts[index].m_DerivedDataKey)
			{
				m_Status.m_Artifacts[index].m_ContentDigest = artifact->m_ContentDigest;
			}
			++artifactCount;
		}

		if (artifactCount == 0)
		{
			m_Status.m_CacheWritePending = !m_PendingCacheWrites.empty();
			return;
		}

		work->m_Task = m_TaskSystem->Submit(
			{
				.m_Name = std::format("IBL.StageDDCWrite: generation {} ({} stages)",
					work->m_Generation, artifactCount),
				.m_Priority = TaskPriority::Background,
			},
			[this, work](std::stop_token stopToken) noexcept
			{
				for (IBLArtifactStage stage : ArtifactStages)
				{
					if (stopToken.stop_requested())
					{
						return TaskResult::Success();
					}
					const size_t index = static_cast<size_t>(stage);
					const IBLStageArtifactHandle& artifact = work->m_Artifacts.Get(stage);
					if (artifact && !m_DerivedDataSystem.Store(work->m_Keys[index], artifact))
					{
						return TaskResult::Failure(
							std::format("Failed to write the {} IBL stage to the local DDC.",
								GetIBLArtifactStageName(stage)));
					}
				}
				return TaskResult::Success();
			},
			[this, work](const TaskCompletionInfo& completion) noexcept
			{ CompleteCacheWrite(completion, work); });
		if (!work->m_Task.IsValid())
		{
			GGLAB_LOG_GRAPHICS_WARN(
				"Failed to submit the IBL stage DDC writer for generation {}.", work->m_Generation);
			m_Status.m_CacheWritePending = !m_PendingCacheWrites.empty();
			return;
		}
		m_PendingCacheWrites.push_back(work);
		m_Status.m_CacheWritePending = true;
	}

	void IBLBakeScheduler::CompleteCacheWrite(
		const TaskCompletionInfo& completion, const std::shared_ptr<CacheWriteWork>& work) noexcept
	{
		const auto pending = std::ranges::find(m_PendingCacheWrites, work);
		if (pending != m_PendingCacheWrites.end())
		{
			m_PendingCacheWrites.erase(pending);
		}
		if (completion.m_Status != TaskStatus::Succeeded)
		{
			GGLAB_LOG_GRAPHICS_WARN(
				"IBL stage artifacts for generation {} were not fully persisted: {}",
				work->m_Generation,
				completion.m_Status == TaskStatus::Cancelled ? "task cancelled"
				: completion.m_Error);
		}
		m_Status.m_CacheWritePending = m_CacheReadbackInFlight || !m_PendingCacheWrites.empty();
	}

	void IBLBakeScheduler::PublishBake() noexcept
	{
		GGLAB_ASSERT_MSG(std::ranges::none_of(m_Status.m_Artifacts,
			[](const IBLStageArtifactStatus& artifact) noexcept
			{ return artifact.m_Resolution == IBLArtifactResolution::Miss; }),
			"IBL staging resources must contain a complete generation before publication.");
		m_RenderResourceRegistry->PublishIBLBakeResources();
		m_Status.m_ActiveGeneration = m_Status.m_BakingGeneration;
		m_Status.m_HasActiveBake = true;
		SetStage(IBLBakeStage::Ready, 1.0f);
		GGLAB_LOG_GRAPHICS_INFO(
			"IBL bake generation {} published atomically (cacheStages={}, gpuStages={}, gpuMs={:.3f}).",
			m_Status.m_ActiveGeneration, m_Status.m_CacheHitStageCount,
			m_Status.m_GpuBuildStageCount, m_Status.m_GpuMilliseconds);
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
		case IBLBakeStage::Environment:
			match = "IBL.Environment";
			break;
		case IBLBakeStage::EnvironmentMipChain:
			match = "IBL.EnvironmentMipChain";
			break;
		case IBLBakeStage::Irradiance:
			match = "IBL.Irradiance";
			break;
		case IBLBakeStage::PrefilteredSpecular:
			match = "IBL.PrefilteredSpecular";
			break;
		case IBLBakeStage::BrdfLut:
			match = "IBL.BrdfLUT";
			break;
		default:
			return;
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
		return stage == IBLBakeStage::Environment || stage == IBLBakeStage::EnvironmentMipChain ||
			stage == IBLBakeStage::Irradiance || stage == IBLBakeStage::PrefilteredSpecular ||
			stage == IBLBakeStage::BrdfLut;
	}
}
