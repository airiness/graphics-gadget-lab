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
			static_assert(std::is_trivially_copyable_v<T>);
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
	// was issued for; once that batch is detached or replaced, uploads return invalid
	// bindings, log and fail any batch that is attached instead of reaching another frame.
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
			static_assert(std::is_trivially_copyable_v<T>);
			return UploadConstants(std::span<const std::byte>(std::as_bytes(data)));
		}
		template <typename T>
		[[nodiscard]] RHIConstantBufferBinding UploadConstants(const T& data) const noexcept
		{
			static_assert(std::is_trivially_copyable_v<T> && std::is_standard_layout_v<T>);
			static_assert(sizeof(T) % sizeof(uint32_t) == 0);
			return UploadConstants(std::as_bytes(std::span<const T>(&data, 1)));
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
