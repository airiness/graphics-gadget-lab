#pragma once
#include "Capture/CaptureProductEncoder.h"
#include "Capture/CaptureBundle.h"
#include "GGLabFoundation/Base/CoreMacros.h"

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace gglab
{
	struct CaptureBundleProductReceipt
	{
		uint32_t m_ProductIndex = 0;
		std::vector<std::filesystem::path> m_Files;
	};

	struct CaptureWriteReceipt
	{
		CapturePublicationOutcome m_Publication = CapturePublicationOutcome::NotPublished;
		std::filesystem::path m_ImagePath;
		std::filesystem::path m_MetadataPath;
		std::vector<std::filesystem::path> m_ResourcePaths;
		std::filesystem::path m_GraphPath;
		std::string m_Failure;
		std::string m_BundleId;
		std::filesystem::path m_ManifestPath;
		std::vector<CaptureBundleProductReceipt> m_BundleProducts;
	};

	struct CaptureWriteJob
	{
		CaptureProductEncodeInput m_Products;
		std::optional<CaptureBundleInput> m_Bundle;
		std::filesystem::path m_Directory;
		std::string m_BaseStem;
		uint64_t m_RequestId = 0;
		CapturePublicationControl m_Publication;
		std::chrono::steady_clock::time_point m_SubmittedAt{};
		// Frame-thread reporting only; the worker never touches this flag.
		bool m_StallReported = false;

		[[nodiscard]] std::optional<CaptureWriteReceipt> GetReceipt() const noexcept;
		// Shares the receipt lock so an already finished pre-claim failure cannot
		// be reclassified as cancelled. Publication itself arbitrates via its claim.
		[[nodiscard]] bool TryCancel() noexcept;
		void WaitUntil(std::chrono::steady_clock::time_point deadline) noexcept;

	private:
		friend class CaptureWriter;
		mutable std::mutex m_Mutex;
		std::condition_variable m_Condition;
		std::optional<CaptureWriteReceipt> m_Receipt;
	};

	// One independently owned I/O worker. The coordinator reserves capacity from
	// issue through watch/encoding; this bounded queue is the final handoff gate.
	class CaptureWriter final
	{
	public:
		static constexpr uint32_t MaxPendingJobs = 8;
		static constexpr uint32_t MaxInFlightJobs = MaxPendingJobs + 1;
		CaptureWriter(FrameCaptureImageEncoder encoder, bool writeOnCallingThread,
			std::shared_ptr<CapturePublicationFileSystemBase> files = {}) noexcept;
		~CaptureWriter();
		GGLAB_DELETE_COPYABLE_MOVABLE(CaptureWriter);

		[[nodiscard]] bool Submit(std::shared_ptr<CaptureWriteJob> job) noexcept;
		// After the owner's total deadline, snapshot a truthful publication outcome.
		// The worker still owns its products and may finish after this receipt.
		[[nodiscard]] CaptureWriteReceipt Abandon(CaptureWriteJob& job,
			std::chrono::milliseconds timeout) noexcept;

	private:
		struct State;
		static void Run(std::shared_ptr<State> state) noexcept;
		static void Write(CaptureWriteJob& job, const State& state) noexcept;
		std::shared_ptr<State> m_State;
		std::thread m_Thread;
		bool m_WriteOnCallingThread = false;
		bool m_HasAbandonedJobs = false;
	};
}
