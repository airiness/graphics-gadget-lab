#pragma once
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
	class TransferManager;

	class IBLBakeScheduler
	{
	public:
		struct CreateInfo
		{
			RHIDevice* m_Device = nullptr;
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
		void OnFrameSubmitted(const RHIFencePoint& fencePoint) noexcept;
		void OnFrameAborted() noexcept;

		[[nodiscard]] IBLBakeStage GetStageForRecording() const noexcept;
		[[nodiscard]] uint64_t GetBakingGeneration() const noexcept { return m_Status.m_BakingGeneration; }
		[[nodiscard]] const IBLBakeConfig& GetBakingConfig() const noexcept { return m_BakingConfig; }
		[[nodiscard]] const IBLBakeStatus& GetStatus() const noexcept { return m_Status; }

	private:
		void StartRequestedBake(const RHIFencePoint& retireFence) noexcept;
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
		bool m_CacheUploadInFlight = false;
		bool m_CacheReadbackInFlight = false;

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
