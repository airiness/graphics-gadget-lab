#pragma once
#include "GGLabFoundation/Base/CoreMacros.h"
#include "GGLabRuntime/Graphics/RHI/RHIBuffer.h"
#include "GGLabRuntime/Graphics/RHI/RHIFence.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

namespace gglab
{
	class RHIDevice;
	class RHIConstantUploadBatch;

	struct RHIConstantBufferBinding
	{
		RHIBufferHandle m_Buffer{};
		uint64_t m_OffsetInBytes = 0;
		uint32_t m_SizeInBytes = 0;
		// Zero identifies caller-owned storage. Upload bindings belong to one batch.
		uint64_t m_UploadBatchId = 0;

		[[nodiscard]] bool IsValid() const noexcept
		{
			return m_Buffer.IsValid() && m_SizeInBytes != 0;
		}
	};

	struct RHIConstantUploadConfig
	{
		uint32_t m_PageSizeInBytes = 64 * 1024;
		uint32_t m_MaxPagesPerBatch = 256;
		uint32_t m_MaxPoolPages = 1024;
	};

	// Owner-thread component shared by backend implementations. Pages are never
	// reused until every command consuming the batch has completed.
	class RHIConstantUploadPool
	{
	public:
		explicit RHIConstantUploadPool(RHIDevice& device,
			const RHIConstantUploadConfig& config = {}) noexcept;
		GGLAB_DELETE_COPYABLE_MOVABLE(RHIConstantUploadPool);
		~RHIConstantUploadPool() noexcept;

		[[nodiscard]] std::unique_ptr<RHIConstantUploadBatch> BeginBatch() noexcept;
		void ReclaimCompleted() noexcept;
		[[nodiscard]] size_t GetPageCount() const noexcept { return m_Pages.size(); }
		[[nodiscard]] size_t GetPendingPageCount() const noexcept;

	private:
		friend class RHIConstantUploadBatch;
		struct Page
		{
			RHIBufferHandle m_Buffer{};
			uint64_t m_CapacityInBytes = 0;
			RHIFencePoint m_Fence{};
			bool m_Leased = false;
		};
		[[nodiscard]] size_t AcquirePage(uint64_t minimumSize) noexcept;

		RHIDevice* m_Device = nullptr;
		RHIConstantUploadConfig m_Config{};
		std::vector<Page> m_Pages;
	};

	class RHIConstantUploadBatch
	{
	public:
		GGLAB_DELETE_COPYABLE_MOVABLE(RHIConstantUploadBatch);
		~RHIConstantUploadBatch() noexcept;

		[[nodiscard]] RHIConstantBufferBinding Upload(std::span<const std::byte> bytes) noexcept;
		// Freezes allocation and publishes all mapped writes before queue submission.
		[[nodiscard]] bool PublishWrites() noexcept;
		// The fence must cover all consumers, including any joined compute work.
		[[nodiscard]] bool Submitted(const RHIFencePoint& fence) noexcept;
		// Only after the command stream referencing these bindings has been discarded.
		void Cancel() noexcept;
		void Fail() noexcept { m_Failed = true; }
		[[nodiscard]] bool HasFailed() const noexcept { return m_Failed; }
		[[nodiscard]] bool Owns(const RHIConstantBufferBinding& binding) const noexcept;
		[[nodiscard]] uint64_t GetId() const noexcept { return m_Id; }
		[[nodiscard]] size_t GetPageCount() const noexcept { return m_Leases.size(); }

	private:
		friend class RHIConstantUploadPool;
		explicit RHIConstantUploadBatch(RHIConstantUploadPool& pool) noexcept;
		enum class State : uint8_t
		{
			Recording,
			Published,
			Submitted,
			Cancelled
		};
		struct PageLease
		{
			size_t m_PageIndex = 0;
			std::byte* m_MappedData = nullptr;
			uint64_t m_CursorInBytes = 0;
		};

		RHIConstantUploadPool* m_Pool = nullptr;
		std::vector<PageLease> m_Leases;
		uint64_t m_Id = 0;
		State m_State = State::Recording;
		bool m_Failed = false;
	};
}
