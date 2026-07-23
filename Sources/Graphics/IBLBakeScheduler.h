#pragma once
#include "Core/Task/TaskTypes.h"
#include "Graphics/Asset/DerivedData/IBLDerivedDataSystem.h"
#include "Graphics/EnvironmentLightingSystem.h"
#include "Graphics/IBLBakeTypes.h"
#include "Graphics/RHI/RHIFence.h"

#include <array>
#include <filesystem>
#include <memory>
#include <vector>

namespace gglab
{
	namespace detail
	{
		struct IBLBakeResourceInitializationState
		{
			[[nodiscard]] constexpr bool ResetForRequestedBake() noexcept
			{
				if (m_InFlight)
				{
					return false;
				}
				*this = {};
				return true;
			}

			[[nodiscard]] constexpr bool Begin(uint64_t generation) noexcept
			{
				if (generation == 0 || m_NeedsRecording || m_Executed || m_InFlight)
				{
					return false;
				}
				m_Generation = generation;
				m_NeedsRecording = true;
				return true;
			}

			[[nodiscard]] constexpr bool ShouldRecord(uint64_t generation) const noexcept
			{
				return m_NeedsRecording && m_Generation == generation;
			}

			[[nodiscard]] constexpr bool NotifyExecuted(uint64_t generation) noexcept
			{
				if (!ShouldRecord(generation))
				{
					return false;
				}
				m_Executed = true;
				return true;
			}

			[[nodiscard]] constexpr bool HasExecuted() const noexcept
			{
				return m_Executed;
			}

			[[nodiscard]] constexpr bool Submit() noexcept
			{
				if (!m_Executed)
				{
					return false;
				}
				m_NeedsRecording = false;
				m_Executed = false;
				m_InFlight = true;
				return true;
			}

			constexpr void AbortFrame() noexcept
			{
				m_Executed = false;
			}

			[[nodiscard]] constexpr bool IsInFlight() const noexcept
			{
				return m_InFlight;
			}

			[[nodiscard]] constexpr uint64_t Complete() noexcept
			{
				if (!m_InFlight)
				{
					return 0;
				}
				const uint64_t generation = m_Generation;
				*this = {};
				return generation;
			}

		private:
			uint64_t m_Generation = 0;
			bool m_NeedsRecording = false;
			bool m_Executed = false;
			bool m_InFlight = false;
		};
	}

	class AssetManager;
	class AssetOwnerScope;
	class GpuProfiler;
	class RHIDevice;
	class RenderResourceRegistry;
	class TaskSystem;
	class TransferManager;

	class IBLBakeScheduler
	{
	private:
		struct BakeRequestSnapshot
		{
			uint64_t m_Generation = 0;
			EnvironmentTextureSource m_Source{};
			IBLBakeConfig m_Config{};
			bool m_IgnoreCache = false;

			[[nodiscard]] bool IsValid() const noexcept
			{
				return m_Generation != 0 && m_Source.IsValid();
			}
		};

	public:
		struct CreateInfo
		{
			RHIDevice* m_Device = nullptr;
			TaskSystem* m_TaskSystem = nullptr;
			EnvironmentLightingSystem* m_EnvironmentLightingSystem = nullptr;
			RenderResourceRegistry* m_RenderResourceRegistry = nullptr;
			TransferManager* m_TransferManager = nullptr;
			GpuProfiler* m_GpuProfiler = nullptr;
			std::filesystem::path m_DerivedDataCacheDirectory;
			IBLStageArtifactCacheConfig m_ArtifactCache{};
		};

		explicit IBLBakeScheduler(const CreateInfo& createInfo) noexcept;
		GGLAB_DELETE_COPYABLE_MOVABLE(IBLBakeScheduler);
		~IBLBakeScheduler();

		void AttachAssetManager(AssetManager& assetManager) noexcept;
		void DetachAssetManager() noexcept;
		void Tick(const RHIFencePoint& lastSubmittedFence) noexcept;
		void NotifyStageExecuted(IBLBakeStage stage, uint64_t generation) noexcept;
		void NotifyBakeResourcesInitialized(uint64_t generation) noexcept;
		void OnFrameSubmitted(const RHIFencePoint& fencePoint) noexcept;
		void OnFrameAborted() noexcept;

		[[nodiscard]] IBLBakeStage GetStageForRecording() const noexcept;
		[[nodiscard]] bool ShouldInitializeBakeResources() const noexcept
		{
			return m_BakeResourceInitialization.ShouldRecord(
				m_Status.m_BakingGeneration);
		}
		[[nodiscard]] uint64_t GetBakingGeneration() const noexcept
		{
			return m_Status.m_BakingGeneration;
		}
		[[nodiscard]] const IBLBakeConfig& GetBakingConfig() const noexcept
		{
			return m_BakingRequest.m_Config;
		}
		[[nodiscard]] const EnvironmentTextureSource& GetBakingSource() const noexcept
		{
			return m_BakingRequest.m_Source;
		}
		[[nodiscard]] const IBLBakeStatus& GetStatus() const noexcept { return m_Status; }
		[[nodiscard]] IBLStageArtifactCacheStatistics GetArtifactCacheStatistics() const noexcept
		{
			return m_DerivedDataSystem.GetArtifactCacheStatistics();
		}
		[[nodiscard]] LocalDerivedDataStoreStatistics GetDerivedDataStoreStatistics() const noexcept
		{
			return m_DerivedDataSystem.GetStoreStatistics();
		}
		void ClearArtifactCache() noexcept { m_DerivedDataSystem.ClearArtifactCache(); }
		[[nodiscard]] bool ClearDerivedDataStore() noexcept
		{
			return m_DerivedDataSystem.ClearStore();
		}

	private:
		struct CacheLoadWork;
		struct CacheWriteWork;

		void StartRequestedBake(const RHIFencePoint& retireFence) noexcept;
		void CompleteCacheLookup(
			const TaskCompletionInfo& completion,
			const std::shared_ptr<CacheLoadWork>& work) noexcept;
		void BeginBakeResourceInitialization(
			const RHIFencePoint& retireFence,
			const std::shared_ptr<CacheLoadWork>& work) noexcept;
		void ContinueRequestedBakeAfterInitialization(uint64_t generation) noexcept;
		void AdvanceCompletedStage() noexcept;
		void AdvanceToNextMissingStage() noexcept;
		void MarkGpuStageBuilt(IBLArtifactStage stage) noexcept;
		bool UploadCachedArtifacts(const IBLDerivedDataLookupResult& result) noexcept;
		bool StartCacheReadback() noexcept;
		void StartCacheWrite() noexcept;
		void CompleteCacheWrite(
			const TaskCompletionInfo& completion,
			const std::shared_ptr<CacheWriteWork>& work) noexcept;
		void PublishBake() noexcept;
		void CaptureGpuTime(IBLBakeStage stage) noexcept;
		void ReleaseBakingSourceLease() noexcept;
		void SetStage(IBLBakeStage stage, float progress) noexcept;
		[[nodiscard]] bool IsGpuStage(IBLBakeStage stage) const noexcept;

		RHIDevice* m_Device = nullptr;
		TaskSystem* m_TaskSystem = nullptr;
		EnvironmentLightingSystem* m_EnvironmentLightingSystem = nullptr;
		RenderResourceRegistry* m_RenderResourceRegistry = nullptr;
		TransferManager* m_TransferManager = nullptr;
		GpuProfiler* m_GpuProfiler = nullptr;
		IBLDerivedDataSystem m_DerivedDataSystem;

		std::unique_ptr<AssetOwnerScope> m_BakingSourceOwner;
		BakeRequestSnapshot m_BakingRequest{};
		IBLBakeStatus m_Status{};
		RHIFencePoint m_InFlightFence{};
		IBLBakeStage m_CompletedStage = IBLBakeStage::Idle;
		IBLBakeStage m_ExecutedStage = IBLBakeStage::Idle;
		detail::IBLBakeResourceInitializationState m_BakeResourceInitialization;
		bool m_CacheUploadInFlight = false;
		bool m_CacheReadbackInFlight = false;

		struct CacheLoadWork
		{
			uint64_t m_Generation = 0;
			IBLDerivedDataLookupResult m_Result;
		};
		TaskHandle m_CacheLookupTask{};
		std::shared_ptr<CacheLoadWork> m_CompletedCacheLookup;
		std::shared_ptr<CacheLoadWork> m_CurrentCacheLoad;

		struct CacheWriteWork
		{
			uint64_t m_Generation = 0;
			IBLBakeConfig m_Config{};
			std::array<DerivedDataKey,
				static_cast<size_t>(IBLArtifactStage::Count)> m_Keys{};
			std::array<bool,
				static_cast<size_t>(IBLArtifactStage::Count)> m_BuiltStages{};
			std::array<RHITextureReadbackRequest,
				static_cast<size_t>(IBLArtifactStage::Count)> m_Requests{};
			IBLStageArtifactSet m_Artifacts;
			TaskHandle m_Task{};
		};
		std::shared_ptr<CacheWriteWork> m_ReadbackWork;
		std::vector<std::shared_ptr<CacheWriteWork>> m_PendingCacheWrites;
	};
}
