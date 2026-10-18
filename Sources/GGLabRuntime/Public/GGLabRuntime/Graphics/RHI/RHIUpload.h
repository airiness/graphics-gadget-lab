#pragma once
#include "GGLabFoundation/Base/CoreMacros.h"
#include "GGLabRuntime/Graphics/RHI/RHIBuffer.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <type_traits>

namespace gglab
{
	class RHIFrameUploads;
	class RHIUploadBatch;

	// Identifies one upload batch. Ids come from a global monotonic counter and are never
	// reused; zero identifies caller-owned storage.
	using RHIUploadBatchId = uint64_t;

	struct RHIUploadLimits
	{
		uint32_t m_ConstantOffsetAlignment = 0;
		uint32_t m_MaxConstantRangeInBytes = 0;
		uint32_t m_StorageOffsetAlignment = 0;
		uint32_t m_MaxStorageRangeInBytes = 0;
	};

	struct RHIUploadPageStatistics
	{
		uint64_t m_PageCount = 0;
		uint64_t m_CapacityInBytes = 0;
	};

	struct RHIUploadBatchStatistics
	{
		uint64_t m_UploadCount = 0;
		uint64_t m_RequestedBytes = 0;
		uint64_t m_ReservedBytes = 0;
		uint64_t m_PageCount = 0;
	};

	struct RHIUploadStatistics
	{
		RHIUploadPageStatistics m_Total;
		RHIUploadPageStatistics m_Pending;
		RHIUploadPageStatistics m_Leased;
		RHIUploadPageStatistics m_Idle;
		RHIUploadPageStatistics m_Quarantined;
		// Most recently ended batch; page states above describe the current pool.
		RHIUploadBatchStatistics m_LastBatch;
		uint64_t m_HighWaterReservedBytesPerBatch = 0;
		uint64_t m_OversizedUploadFailures = 0;
		uint64_t m_BatchByteBudgetFailures = 0;
		uint64_t m_PoolByteBudgetFailures = 0;
		uint64_t m_PageCountFailures = 0;
		uint64_t m_PageCreationFailures = 0;
		uint64_t m_PageMappingFailures = 0;
		uint64_t m_PublicationFailures = 0;
		uint64_t m_SubmittedBatchCount = 0;
		uint64_t m_CancelledBatchCount = 0;
	};

	struct RHIConstantBufferBinding
	{
		RHIBufferHandle m_Buffer{};
		uint64_t m_OffsetInBytes = 0;
		uint32_t m_SizeInBytes = 0;
		// Upload bindings belong to one batch and must not be kept beyond its frame.
		RHIUploadBatchId m_UploadBatchId = 0;

		[[nodiscard]] bool IsValid() const noexcept
		{
			return m_Buffer.IsValid() && m_SizeInBytes != 0;
		}
	};

	struct RHIReadOnlyBufferBinding
	{
		RHIBufferHandle m_Buffer{};
		uint64_t m_OffsetInBytes = 0;
		uint32_t m_SizeInBytes = 0;
		uint32_t m_StrideInBytes = 0;
		RHIUploadBatchId m_UploadBatchId = 0;

		[[nodiscard]] bool IsValid() const noexcept
		{
			return m_Buffer.IsValid() && m_SizeInBytes != 0 && m_StrideInBytes != 0;
		}
	};

	[[nodiscard]] inline bool IsConstantBufferBindingInRange(const RHIConstantBufferBinding& binding,
		uint64_t bufferSize, const RHIUploadLimits& limits) noexcept
	{
		return binding.IsValid() && binding.m_SizeInBytes % sizeof(uint32_t) == 0 &&
			limits.m_ConstantOffsetAlignment != 0 &&
			binding.m_OffsetInBytes % limits.m_ConstantOffsetAlignment == 0 &&
			binding.m_SizeInBytes <= limits.m_MaxConstantRangeInBytes &&
			binding.m_OffsetInBytes <= bufferSize &&
			binding.m_SizeInBytes <= bufferSize - binding.m_OffsetInBytes;
	}

	[[nodiscard]] inline bool IsReadOnlyBufferBindingInRange(const RHIReadOnlyBufferBinding& binding,
		uint64_t bufferSize, const RHIUploadLimits& limits) noexcept
	{
		return binding.IsValid() && binding.m_StrideInBytes % sizeof(uint32_t) == 0 &&
			binding.m_SizeInBytes % binding.m_StrideInBytes == 0 &&
			limits.m_StorageOffsetAlignment != 0 &&
			binding.m_OffsetInBytes % limits.m_StorageOffsetAlignment == 0 &&
			binding.m_SizeInBytes <= limits.m_MaxStorageRangeInBytes &&
			binding.m_OffsetInBytes <= bufferSize &&
			binding.m_SizeInBytes <= bufferSize - binding.m_OffsetInBytes;
	}

	// Writes per-submission data into the upload batch its submission owner attached. Each
	// backend frame context owns one for its lifetime. Only submission owners and encoders
	// hold a pointer to it; producers receive RHIFrameUploads instead.
	class RHIUploadWriter final
	{
	public:
		RHIUploadWriter() noexcept = default;
		GGLAB_DELETE_COPYABLE_MOVABLE(RHIUploadWriter);
		~RHIUploadWriter() noexcept;

		[[nodiscard]] RHIConstantBufferBinding UploadConstants(
			std::span<const std::byte> bytes) noexcept;
		template <typename T, size_t Extent>
		[[nodiscard]] RHIConstantBufferBinding UploadConstants(std::span<T, Extent> data) noexcept
		{
			// Element size may be smaller than a word; the byte overload checks the total size.
			static_assert(std::is_trivially_copyable_v<T> && std::is_standard_layout_v<T>);
			return UploadConstants(std::span<const std::byte>(std::as_bytes(data)));
		}
		template <typename T>
		[[nodiscard]] RHIConstantBufferBinding UploadConstants(const T& data) noexcept
		{
			static_assert(std::is_trivially_copyable_v<T> && std::is_standard_layout_v<T>);
			static_assert(sizeof(T) % sizeof(uint32_t) == 0);
			return UploadConstants(std::as_bytes(std::span<const T>(&data, 1)));
		}

		// True when the binding lies in a range this writer's recording batch wrote.
		[[nodiscard]] bool Owns(const RHIConstantBufferBinding& binding) const noexcept;
		[[nodiscard]] RHIReadOnlyBufferBinding UploadStructured(
			std::span<const std::byte> bytes, uint32_t strideInBytes) noexcept;
		template <typename T, size_t Extent>
		[[nodiscard]] RHIReadOnlyBufferBinding UploadStructured(std::span<T, Extent> elements) noexcept
		{
			static_assert(std::is_trivially_copyable_v<T> && std::is_standard_layout_v<T>);
			static_assert(sizeof(T) % sizeof(uint32_t) == 0);
			return UploadStructured(std::as_bytes(elements), sizeof(T));
		}
		[[nodiscard]] bool Owns(const RHIReadOnlyBufferBinding& binding) const noexcept;
		[[nodiscard]] bool IsUploadStorage(RHIBufferHandle buffer) const noexcept;
		[[nodiscard]] bool IsRecording() const noexcept;
		[[nodiscard]] bool HasFailed() const noexcept;
		// An encoder failure poisons the batch shared by every encoder of the submission.
		void Fail() noexcept;
		// Zero while no batch is attached.
		[[nodiscard]] RHIUploadBatchId GetBatchId() const noexcept;
		// A producer capability bound to the currently attached batch.
		[[nodiscard]] RHIFrameUploads GetFrameUploads() noexcept;

	private:
		friend class RHIUploadBatch;

		RHIUploadBatch* m_Batch = nullptr;
	};

	// Copyable producer capability for one frame's upload batch. It records the batch id it
	// was issued for; once that batch is detached or replaced, uploads log and return invalid
	// bindings instead of reaching another frame. When the issuing writer already serves a
	// later batch, that batch is failed as well; a backend may use one writer per frame slot,
	// so a stale capability is not guaranteed to fail the frame that is recording now. It
	// borrows its writer and must not outlive the RHI context that issued it.
	class RHIFrameUploads
	{
	public:
		RHIFrameUploads() noexcept = default;

		[[nodiscard]] RHIConstantBufferBinding UploadConstants(
			std::span<const std::byte> bytes) const noexcept;
		template <typename T, size_t Extent>
		[[nodiscard]] RHIConstantBufferBinding UploadConstants(
			std::span<T, Extent> data) const noexcept
		{
			// Element size may be smaller than a word; the byte overload checks the total size.
			static_assert(std::is_trivially_copyable_v<T> && std::is_standard_layout_v<T>);
			return UploadConstants(std::span<const std::byte>(std::as_bytes(data)));
		}
		template <typename T>
		[[nodiscard]] RHIConstantBufferBinding UploadConstants(const T& data) const noexcept
		{
			static_assert(std::is_trivially_copyable_v<T> && std::is_standard_layout_v<T>);
			static_assert(sizeof(T) % sizeof(uint32_t) == 0);
			return UploadConstants(std::as_bytes(std::span<const T>(&data, 1)));
		}

		[[nodiscard]] RHIReadOnlyBufferBinding UploadStructured(
			std::span<const std::byte> bytes, uint32_t strideInBytes) const noexcept;
		template <typename T, size_t Extent>
		[[nodiscard]] RHIReadOnlyBufferBinding UploadStructured(std::span<T, Extent> elements) const noexcept
		{
			static_assert(std::is_trivially_copyable_v<T> && std::is_standard_layout_v<T>);
			static_assert(sizeof(T) % sizeof(uint32_t) == 0);
			return UploadStructured(std::as_bytes(elements), sizeof(T));
		}
		// True while the issuing writer is still attached to the batch it was issued for.
		[[nodiscard]] bool IsCurrent() const noexcept;
		[[nodiscard]] RHIUploadBatchId GetBatchId() const noexcept { return m_BatchId; }

	private:
		friend class RHIUploadWriter;
		RHIFrameUploads(RHIUploadWriter& writer, RHIUploadBatchId batchId) noexcept :
			m_Writer(&writer), m_BatchId(batchId)
		{
		}

		RHIUploadWriter* m_Writer = nullptr;
		RHIUploadBatchId m_BatchId = 0;
	};
}
