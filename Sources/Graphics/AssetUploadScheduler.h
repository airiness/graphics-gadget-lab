#pragma once
#include "Core/Async/ProgressChannel.h"
#include "Graphics/RHI/RHIFence.h"

#include <chrono>
#include <cstdint>
#include <deque>
#include <functional>
#include <string>
#include <thread>
#include <vector>

namespace gglab
{
	class RHIDevice;
	class TransferBatch;
	class TransferManager;

	struct AssetUploadHandle
	{
		uint64_t m_Value = 0;

		[[nodiscard]] constexpr bool IsValid() const noexcept { return m_Value != 0; }
		explicit constexpr operator bool() const noexcept { return IsValid(); }
		friend constexpr auto operator<=>(const AssetUploadHandle&, const AssetUploadHandle&) = default;
	};

	enum class AssetUploadStatus : uint8_t
	{
		Pending,
		Succeeded,
		Failed,
	};

	struct AssetUploadDesc
	{
		std::string m_Name;
		ProgressChannelPtr m_Progress;
	};

	struct AssetUploadCompletionInfo
	{
		AssetUploadHandle m_Handle{};
		std::string m_Name;
		AssetUploadStatus m_Status = AssetUploadStatus::Failed;
		RHIFencePoint m_FencePoint{};
		double m_ElapsedMilliseconds = 0.0;
	};

	using AssetUploadCompletion = std::function<void(const AssetUploadCompletionInfo&)>;

	struct AssetUploadActivity
	{
		AssetUploadHandle m_Handle{};
		std::string m_Name;
		AssetUploadStatus m_Status = AssetUploadStatus::Pending;
		RHIFencePoint m_FencePoint{};
		double m_ElapsedMilliseconds = 0.0;
		ProgressSnapshot m_Progress;
	};

	struct AssetUploadStatistics
	{
		uint32_t m_PendingCount = 0;
		uint64_t m_SubmittedCount = 0;
		uint64_t m_SucceededCount = 0;
		uint64_t m_FailedCount = 0;
		uint64_t m_CompletionCallbackFailureCount = 0;
		std::vector<AssetUploadActivity> m_PendingUploads;
		std::vector<AssetUploadActivity> m_RecentUploads;
	};

	// Owns the main-thread publication boundary for transfer-queue work. Callers
	// record a batch, submit without waiting, then publish assets from the fence callback.
	class AssetUploadScheduler
	{
	public:
		struct CreateInfo
		{
			RHIDevice* m_Device = nullptr;
			TransferManager* m_TransferManager = nullptr;
			uint32_t m_RecentUploadCapacity = 64;
		};

		explicit AssetUploadScheduler(const CreateInfo& createInfo) noexcept;
		GGLAB_DELETE_COPYABLE_MOVABLE(AssetUploadScheduler);
		~AssetUploadScheduler();

		[[nodiscard]] AssetUploadHandle Submit(
			AssetUploadDesc desc,
			TransferBatch&& batch,
			bool recordingSucceeded,
			AssetUploadCompletion completion = {}) noexcept;
		uint32_t Tick() noexcept;
		void Finalize() noexcept;

		[[nodiscard]] AssetUploadStatistics GetStatistics() const;

	private:
		struct PendingUpload
		{
			AssetUploadHandle m_Handle{};
			AssetUploadDesc m_Desc;
			RHIFencePoint m_FencePoint{};
			std::chrono::steady_clock::time_point m_SubmittedAt{};
			bool m_RecordingSucceeded = false;
			AssetUploadCompletion m_Completion;
		};

		[[nodiscard]] bool IsOwnerThread() const noexcept;
		void FinishUpload(PendingUpload&& upload, AssetUploadStatus status) noexcept;

	private:
		RHIDevice* m_Device = nullptr;
		TransferManager* m_TransferManager = nullptr;
		std::thread::id m_OwnerThreadId;
		uint32_t m_RecentUploadCapacity = 0;
		uint64_t m_NextHandle = 1;
		uint64_t m_SubmittedCount = 0;
		uint64_t m_SucceededCount = 0;
		uint64_t m_FailedCount = 0;
		uint64_t m_CompletionCallbackFailureCount = 0;
		std::deque<PendingUpload> m_PendingUploads;
		std::deque<AssetUploadActivity> m_RecentUploads;
	};
}
