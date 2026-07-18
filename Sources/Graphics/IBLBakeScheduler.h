#pragma once
#include "Core/Task/TaskTypes.h"
#include "Graphics/IBLBakeCache.h"
#include "Graphics/IBLBakeTypes.h"
#include "Graphics/RHI/RHIFence.h"

#include <filesystem>
#include <array>
#include <future>
#include <memory>
#include <vector>

namespace gglab
{
	class EnvironmentLightingSystem;
	class GpuProfiler;
	class RHIDevice;
	class RenderResourceRegistry;
	class TaskSystem;
	class TransferManager;

	class IBLBakeScheduler
	{
	public:
		struct CreateInfo
		{
			RHIDevice* m_Device = nullptr;
			TaskSystem* m_TaskSystem = nullptr;
			EnvironmentLightingSystem* m_EnvironmentLightingSystem = nullptr;
			RenderResourceRegistry* m_RenderResourceRegistry = nullptr;
			TransferManager* m_TransferManager = nullptr;
			GpuProfiler* m_GpuProfiler = nullptr;
			std::filesystem::path m_CacheDirectory;
		};

		explicit IBLBakeScheduler(const CreateInfo& createInfo) noexcept;
		GGLAB_DELETE_COPYABLE_MOVABLE(IBLBakeScheduler);
		~IBLBakeScheduler();

		void Tick(const RHIFencePoint& lastSubmittedFence) noexcept;
		void NotifyStageExecuted(IBLBakeStage stage, uint64_t generation) noexcept;
		void NotifyBakeResourcesInitialized(uint64_t generation) noexcept;
		void OnFrameSubmitted(const RHIFencePoint& fencePoint) noexcept;
		void OnFrameAborted() noexcept;

		[[nodiscard]] IBLBakeStage GetStageForRecording() const noexcept;
		[[nodiscard]] bool ShouldInitializeBakeResources() const noexcept
		{
			return m_BakeResourcesNeedInitialization;
		}
		[[nodiscard]] uint64_t GetBakingGeneration() const noexcept { return m_Status.m_BakingGeneration; }
		[[nodiscard]] const IBLBakeConfig& GetBakingConfig() const noexcept { return m_BakingConfig; }
		[[nodiscard]] const IBLBakeStatus& GetStatus() const noexcept { return m_Status; }

	private:
		struct CacheLoadWork;

		void StartRequestedBake(const RHIFencePoint& retireFence) noexcept;
		void CompleteCacheLookup(
			const TaskCompletionInfo& completion,
			const std::shared_ptr<CacheLoadWork>& work) noexcept;
		void BeginBakeResourceInitialization(
			const RHIFencePoint& retireFence,
			const std::shared_ptr<CacheLoadWork>& work) noexcept;
		void ContinueRequestedBakeAfterInitialization() noexcept;
		void AdvanceCompletedStage() noexcept;
		bool UploadCachePayload(const IBLBakeCachePayload& payload) noexcept;
		bool StartCacheReadback() noexcept;
		void StartCacheWrite() noexcept;
		void PollCacheWrites() noexcept;
		void PublishBake(bool cacheHit) noexcept;
		void CaptureGpuTime(IBLBakeStage stage) noexcept;
		void SetStage(IBLBakeStage stage, float progress) noexcept;
		[[nodiscard]] bool IsGpuStage(IBLBakeStage stage) const noexcept;

		RHIDevice* m_Device = nullptr;
		TaskSystem* m_TaskSystem = nullptr;
		EnvironmentLightingSystem* m_EnvironmentLightingSystem = nullptr;
		RenderResourceRegistry* m_RenderResourceRegistry = nullptr;
		TransferManager* m_TransferManager = nullptr;
		GpuProfiler* m_GpuProfiler = nullptr;
		IBLBakeCache m_Cache;

		IBLBakeConfig m_BakingConfig{};
		IBLBakeStatus m_Status{};
		RHIFencePoint m_InFlightFence{};
		IBLBakeStage m_CompletedStage = IBLBakeStage::Idle;
		IBLBakeStage m_ExecutedStage = IBLBakeStage::Idle;
		bool m_BakeResourcesNeedInitialization = false;
		bool m_BakeResourceInitializationExecuted = false;
		bool m_BakeResourceInitializationInFlight = false;
		bool m_CacheUploadInFlight = false;
		bool m_CacheReadbackInFlight = false;

		struct CacheLoadWork
		{
			uint64_t m_Generation = 0;
			uint64_t m_Key = 0;
			bool m_CacheHit = false;
			IBLBakeCachePayload m_Payload;
		};
		TaskHandle m_CacheLookupTask{};
		std::shared_ptr<CacheLoadWork> m_CompletedCacheLookup;
		std::shared_ptr<CacheLoadWork> m_CurrentCacheLoad;

		struct CacheWriteWork
		{
			uint64_t m_Key = 0;
			std::array<RHITextureReadbackRequest, 4> m_Requests;
			std::array<const std::byte*, 4> m_Mapped{};
		};
		struct PendingCacheWrite
		{
			std::shared_ptr<CacheWriteWork> m_Work;
			std::future<bool> m_Result;
		};
		std::shared_ptr<CacheWriteWork> m_ReadbackWork;
		std::vector<PendingCacheWrite> m_PendingCacheWrites;
	};
}
