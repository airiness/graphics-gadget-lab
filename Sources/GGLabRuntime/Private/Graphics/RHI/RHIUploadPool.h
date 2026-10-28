#pragma once
#include "GGLabFoundation/Base/CoreMacros.h"
#include "GGLabRuntime/Graphics/RHI/RHIBuffer.h"
#include "GGLabRuntime/Graphics/RHI/RHIFence.h"
#include "GGLabRuntime/Graphics/RHI/RHIUpload.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <thread>
#include <vector>

namespace gglab
{
	class RHIDevice;

	struct RHIUploadConfig
	{
		uint32_t m_PageSizeInBytes = 64 * 1024;
		uint32_t m_MaxUploadSizeInBytes = 64 * 1024;
		uint64_t m_MaxBytesPerBatch = 16ull * 1024 * 1024;
		uint64_t m_MaxPoolBytes = 64ull * 1024 * 1024;
		uint32_t m_MaxPagesPerBatch = 256;
		uint32_t m_MaxPoolPages = 1024;
	};

	// Owner-thread component shared by the submission owners; the constructing thread owns it.
	// Pages stay mapped for their lifetime and are never reused until every command consuming
	// the batch has completed.
	class RHIUploadPool
	{
	public:
		explicit RHIUploadPool(RHIDevice& device, const RHIUploadConfig& config = {}) noexcept;
		GGLAB_DELETE_COPYABLE_MOVABLE(RHIUploadPool);
		~RHIUploadPool() noexcept;

		[[nodiscard]] std::unique_ptr<RHIUploadBatch> BeginBatch() noexcept;
		void ReclaimCompleted() noexcept;
		// Only after every queue that may have read quarantined pages was verified idle.
		void ReleaseQuarantinedAfterVerifiedIdle() noexcept;
		// Only after the native API confirmed device loss: the pages may then be destroyed,
		// but nothing reads them again.
		void ReleaseQuarantinedAfterDeviceLoss() noexcept;
		[[nodiscard]] size_t GetPageCount() const noexcept { return m_Pages.size(); }
		[[nodiscard]] size_t GetPendingPageCount() const noexcept;
		[[nodiscard]] size_t GetQuarantinedPageCount() const noexcept;
		[[nodiscard]] RHIUploadStatistics GetStatistics() const noexcept;
		[[nodiscard]] bool IsOwnerThread() const noexcept
		{
			return std::this_thread::get_id() == m_OwnerThread;
		}

	private:
		friend class RHIUploadBatch;
		struct Page
		{
			RHIBufferHandle m_Buffer{};
			std::byte* m_MappedData = nullptr;
			uint64_t m_CapacityInBytes = 0;
			RHIFencePoint m_Fence{};
			bool m_Leased = false;
			// Possibly read by work without a reliable completion fence.
			bool m_Quarantined = false;
		};
		[[nodiscard]] size_t AcquirePage(uint64_t minimumSize, uint64_t remainingBatchBytes) noexcept;
		void ReleaseQuarantinedPages() noexcept;

		RHIDevice* m_Device = nullptr;
		std::thread::id m_OwnerThread = std::this_thread::get_id();
		RHIUploadConfig m_Config{};
		std::vector<Page> m_Pages;
		struct RetainedResources
		{
			std::vector<std::shared_ptr<void>> m_Resources;
			RHIFencePoint m_Fence{};
			bool m_Quarantined = false;
		};
		std::vector<RetainedResources> m_RetainedResources;
		uint64_t m_CapacityInBytes = 0;
		RHIUploadStatistics m_Statistics{};
	};

	// The pages one submission writes. Only its submission owner drives the transitions; a
	// producer reaches the batch through the attached writer. Uploads from any thread other
	// than the pool's owner return invalid bindings.
	class RHIUploadBatch
	{
	public:
		GGLAB_DELETE_COPYABLE_MOVABLE(RHIUploadBatch);
		// Requires a terminal state and no attached writer.
		~RHIUploadBatch() noexcept;

		// A writer serves one batch at a time; the owner detaches it before the batch ends.
		void Attach(RHIUploadWriter& writer) noexcept;
		void Detach(RHIUploadWriter& writer) noexcept;
		[[nodiscard]] RHIConstantBufferBinding Upload(std::span<const std::byte> bytes) noexcept;
		[[nodiscard]] RHIReadOnlyBufferBinding UploadStructured(
			std::span<const std::byte> bytes, uint32_t strideInBytes) noexcept;
		// Freezes allocation and flushes all leased writes before queue submission.
		[[nodiscard]] bool Publish() noexcept;
		// Recording-only native leases. Off-owner rejection does not mutate batch state.
		[[nodiscard]] bool RetainForSubmission(std::shared_ptr<void> resource) noexcept;
		// Called once the native submit of a stream that may read the batch was issued.
		// From then on the batch can no longer be cancelled.
		void RecordAcceptedSubmission() noexcept { m_HasAcceptedSubmission = true; }
		[[nodiscard]] bool HasAcceptedSubmission() const noexcept { return m_HasAcceptedSubmission; }
		// The fence must cover all consumers, including any joined compute work.
		[[nodiscard]] bool Submitted(const RHIFencePoint& fence) noexcept;
		// Accepted work may read the pages without a reliable completion fence. The pages
		// stay quarantined until the pool is told that every consuming queue is idle.
		void SubmittedWithoutCompletionFence() noexcept;
		// Only while no stream that reads these bindings has been accepted for execution. A
		// batch with an accepted submission is quarantined instead, never released for reuse.
		void Cancel() noexcept;
		void Fail() noexcept { m_Failed = true; }
		[[nodiscard]] bool HasFailed() const noexcept { return m_Failed; }
		[[nodiscard]] bool IsRecording() const noexcept { return m_State == State::Recording; }
		[[nodiscard]] bool Owns(const RHIConstantBufferBinding& binding) const noexcept;
		[[nodiscard]] bool Owns(const RHIReadOnlyBufferBinding& binding) const noexcept;
		[[nodiscard]] bool IsUploadStorage(RHIBufferHandle buffer) const noexcept;
		[[nodiscard]] RHIUploadBatchId GetId() const noexcept { return m_Id; }
		[[nodiscard]] size_t GetPageCount() const noexcept { return m_Leases.size(); }

	private:
		friend class RHIUploadPool;
		explicit RHIUploadBatch(RHIUploadPool& pool) noexcept;
		enum class State : uint8_t
		{
			Recording,
			Published,
			Submitted,
			Quarantined,
			Cancelled
		};
		struct PageLease
		{
			size_t m_PageIndex = 0;
			uint64_t m_CursorInBytes = 0;
		};
		[[nodiscard]] bool AcceptsUploadOnCurrentThread() const noexcept;
		[[nodiscard]] RHIConstantBufferBinding UploadBytes(std::span<const std::byte> bytes,
			uint32_t alignment, uint32_t maximumRange) noexcept;
		void FinishStatistics() noexcept;

		RHIUploadPool* m_Pool = nullptr;
		RHIUploadWriter* m_Writer = nullptr;
		std::vector<PageLease> m_Leases;
		std::vector<std::shared_ptr<void>> m_RetainedResources;
		RHIUploadBatchStatistics m_Statistics{};
		RHIUploadBatchId m_Id = 0;
		State m_State = State::Recording;
		bool m_Failed = false;
		bool m_HasAcceptedSubmission = false;
	};
}
