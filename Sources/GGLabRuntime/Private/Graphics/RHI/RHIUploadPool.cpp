#include "Graphics/RHI/RHIUploadPool.h"
#include "GGLabRuntime/Graphics/RHI/RHIDevice.h"
#include "GGLabRuntime/Core/Log/LogMacros.h"

#include <algorithm>
#include <atomic>
#include <cstring>
#include <limits>

namespace gglab
{
	namespace
	{
		std::atomic<uint64_t> NextUploadBatchId{ 1 };
		constexpr size_t InvalidPage = std::numeric_limits<size_t>::max();
	}

	RHIUploadPool::RHIUploadPool(
		RHIDevice& device, const RHIUploadConfig& config) noexcept :
		m_Device(&device), m_Config(config)
	{
	}

	RHIUploadPool::~RHIUploadPool() noexcept
	{
		ReclaimCompleted();
		if (!m_RetainedResources.empty())
		{
			GGLAB_LOG_GRAPHICS_ERROR_ALWAYS(
				"Submission backing retained without verified completion at upload-pool teardown.");
			// Preserve native objects as well as their allocations on an unverified teardown.
			GGLAB_UNUSED(new std::vector<RetainedResources>(std::move(m_RetainedResources)));
		}
		size_t leakedPageCount = 0;
		for (const auto& page : m_Pages)
		{
			GGLAB_ASSERT_MSG(!page.m_Leased,
				"Upload batches must be submitted or cancelled before pool destruction.");
			// Work without a reliable completion fence may still read a quarantined page, and
			// no recorded use defers its destruction. Leak it rather than free live GPU memory.
			if (page.m_Quarantined)
			{
				++leakedPageCount;
				continue;
			}
			// Every write was published when its batch was; ending the mapping writes nothing.
			m_Device->UnmapBuffer(page.m_Buffer, {});
			m_Device->DestroyBuffer(page.m_Buffer);
		}
		if (leakedPageCount != 0)
		{
			GGLAB_LOG_GRAPHICS_ERROR_ALWAYS(
				"Upload pool leaked {} quarantined pages whose GPU completion was never verified.",
				leakedPageCount);
		}
	}

	std::unique_ptr<RHIUploadBatch> RHIUploadPool::BeginBatch() noexcept
	{
		return std::unique_ptr<RHIUploadBatch>(new RHIUploadBatch(*this));
	}

	void RHIUploadPool::ReclaimCompleted() noexcept
	{
		std::erase_if(m_RetainedResources, [this](const RetainedResources& resources)
			{
				return !resources.m_Quarantined &&
					m_Device->IsFencePointCompleted(resources.m_Fence);
			});
		for (auto& page : m_Pages)
		{
			if (page.m_Fence.IsValid() && m_Device->IsFencePointCompleted(page.m_Fence))
			{
				page.m_Fence = {};
			}
		}
	}

	void RHIUploadPool::ReleaseQuarantinedAfterVerifiedIdle() noexcept
	{
		ReleaseQuarantinedPages();
	}

	void RHIUploadPool::ReleaseQuarantinedAfterDeviceLoss() noexcept
	{
		ReleaseQuarantinedPages();
	}

	void RHIUploadPool::ReleaseQuarantinedPages() noexcept
	{
		m_RetainedResources.clear();
		for (auto& page : m_Pages)
		{
			page.m_Quarantined = false;
		}
	}

	size_t RHIUploadPool::GetQuarantinedPageCount() const noexcept
	{
		return std::ranges::count_if(m_Pages, [](const Page& page)
			{
				return page.m_Quarantined;
			});
	}

	size_t RHIUploadPool::GetPendingPageCount() const noexcept
	{
		return std::ranges::count_if(m_Pages, [](const Page& page)
			{
				return page.m_Fence.IsValid();
			});
	}

	RHIUploadStatistics RHIUploadPool::GetStatistics() const noexcept
	{
		auto statistics = m_Statistics;
		for (const Page& page : m_Pages)
		{
			++statistics.m_Total.m_PageCount;
			statistics.m_Total.m_CapacityInBytes += page.m_CapacityInBytes;
			auto& state = page.m_Quarantined ? statistics.m_Quarantined :
				page.m_Leased ? statistics.m_Leased :
				page.m_Fence.IsValid() ? statistics.m_Pending : statistics.m_Idle;
			++state.m_PageCount;
			state.m_CapacityInBytes += page.m_CapacityInBytes;
		}
		return statistics;
	}

	size_t RHIUploadPool::AcquirePage(uint64_t minimumSize, uint64_t remainingBatchBytes) noexcept
	{
		ReclaimCompleted();
		size_t best = InvalidPage;
		for (size_t index = 0; index < m_Pages.size(); ++index)
		{
			auto& page = m_Pages[index];
			if (!page.m_Leased && !page.m_Quarantined && !page.m_Fence.IsValid() &&
				page.m_CapacityInBytes >= minimumSize && page.m_CapacityInBytes <= remainingBatchBytes &&
				(best == InvalidPage || page.m_CapacityInBytes < m_Pages[best].m_CapacityInBytes))
			{
				best = index;
			}
		}
		if (best != InvalidPage)
		{
			m_Pages[best].m_Leased = true;
			return best;
		}
		const uint64_t capacity = std::max<uint64_t>(minimumSize, m_Config.m_PageSizeInBytes);
		if (capacity > remainingBatchBytes)
		{
			++m_Statistics.m_BatchByteBudgetFailures;
			GGLAB_LOG_GRAPHICS_ERROR("Upload allocation exhausted its batch byte budget.");
			return InvalidPage;
		}
		if (capacity > m_Config.m_MaxPoolBytes || m_CapacityInBytes > m_Config.m_MaxPoolBytes - capacity)
		{
			++m_Statistics.m_PoolByteBudgetFailures;
			GGLAB_LOG_GRAPHICS_ERROR("Upload allocation exhausted its pool byte budget.");
			return InvalidPage;
		}
		if (m_Pages.size() >= m_Config.m_MaxPoolPages || m_Config.m_PageSizeInBytes == 0)
		{
			++m_Statistics.m_PageCountFailures;
			GGLAB_LOG_GRAPHICS_ERROR("Upload page allocation exhausted its budget.");
			return InvalidPage;
		}
		const auto buffer = m_Device->CreateBuffer(
			{
				.m_SizeInBytes = capacity,
				.m_Usage = RHIBufferUsage::Constant | RHIBufferUsage::Structured,
				.m_MemoryUsage = RHIMemoryUsage::CpuToGpu,
				.m_CreateFlags = RHIBufferCreateFlags::UploadStorage,
			},
			{
				.m_Domain = RHIResourceDebugDomain::Renderer,
				.m_Label = "RHI.Upload.Page",
			});
		if (!buffer.IsValid())
		{
			++m_Statistics.m_PageCreationFailures;
			GGLAB_LOG_GRAPHICS_ERROR("Upload page creation failed.");
			return InvalidPage;
		}
		// Pages are mapped once and stay mapped; each batch publishes its leases by flushing.
		auto* mapped = static_cast<std::byte*>(m_Device->MapBuffer(buffer, {}));
		if (!mapped)
		{
			++m_Statistics.m_PageMappingFailures;
			GGLAB_LOG_GRAPHICS_ERROR("Upload page mapping failed.");
			m_Device->DestroyBuffer(buffer);
			return InvalidPage;
		}
		m_Pages.push_back({ buffer, mapped, capacity, {}, true, false });
		m_CapacityInBytes += capacity;
		return m_Pages.size() - 1;
	}

	RHIUploadBatch::RHIUploadBatch(RHIUploadPool& pool) noexcept :
		m_Pool(&pool), m_Id(NextUploadBatchId.fetch_add(1, std::memory_order_relaxed))
	{
	}

	RHIUploadBatch::~RHIUploadBatch() noexcept
	{
		GGLAB_ASSERT_MSG(m_State == State::Submitted || m_State == State::Quarantined ||
			m_State == State::Cancelled,
			"An upload batch requires explicit submission or command-stream cancellation.");
		GGLAB_ASSERT_MSG(!m_Writer, "An upload batch must detach its writer before it ends.");
	}

	void RHIUploadBatch::Attach(RHIUploadWriter& writer) noexcept
	{
		GGLAB_ASSERT_MSG(!m_Writer && !writer.m_Batch,
			"An upload writer and its batch must both be detached before attaching.");
		if (m_Writer || writer.m_Batch)
		{
			Fail();
			return;
		}
		m_Writer = &writer;
		writer.m_Batch = this;
	}

	void RHIUploadBatch::Detach(RHIUploadWriter& writer) noexcept
	{
		GGLAB_ASSERT_MSG(m_Writer == &writer && writer.m_Batch == this,
			"Only the attached upload writer can be detached.");
		if (m_Writer != &writer || writer.m_Batch != this)
		{
			return;
		}
		writer.m_Batch = nullptr;
		m_Writer = nullptr;
	}

	bool RHIUploadBatch::AcceptsUploadOnCurrentThread() const noexcept
	{
		if (m_Pool->IsOwnerThread())
		{
			return true;
		}
		// Detection only: a foreign thread must not touch batch state, so it neither writes
		// nor fails the batch. The encoder that binds the invalid result fails instead.
		GGLAB_LOG_GRAPHICS_ERROR("Upload batch {} rejected an upload off its owner thread.", m_Id);
		return false;
	}

	RHIConstantBufferBinding RHIUploadBatch::Upload(std::span<const std::byte> bytes) noexcept
	{
		if (!AcceptsUploadOnCurrentThread())
		{
			return {};
		}
		const auto limits = m_Pool->m_Device->GetUploadLimits();
		return UploadBytes(bytes, limits.m_ConstantOffsetAlignment, limits.m_MaxConstantRangeInBytes);
	}

	RHIReadOnlyBufferBinding RHIUploadBatch::UploadStructured(
		std::span<const std::byte> bytes, uint32_t strideInBytes) noexcept
	{
		if (!AcceptsUploadOnCurrentThread())
		{
			return {};
		}
		if (strideInBytes == 0 || strideInBytes % sizeof(uint32_t) != 0 || bytes.size() % strideInBytes != 0)
		{
			Fail();
			return {};
		}
		const auto limits = m_Pool->m_Device->GetUploadLimits();
		if (limits.m_StorageOffsetAlignment == 0 || limits.m_ConstantOffsetAlignment == 0)
		{
			Fail();
			return {};
		}
		const auto binding = UploadBytes(bytes,
			std::max(limits.m_ConstantOffsetAlignment, limits.m_StorageOffsetAlignment),
			limits.m_MaxStorageRangeInBytes);
		return { binding.m_Buffer, binding.m_OffsetInBytes, binding.m_SizeInBytes, strideInBytes,
			binding.m_UploadBatchId };
	}

	RHIConstantBufferBinding RHIUploadBatch::UploadBytes(std::span<const std::byte> bytes,
		uint32_t alignment, uint32_t maximumRange) noexcept
	{
		if (m_State != State::Recording || m_Failed || bytes.empty() ||
			bytes.size() % sizeof(uint32_t) != 0 || alignment == 0)
		{
			Fail();
			return {};
		}
		if (bytes.size() > maximumRange || bytes.size() > m_Pool->m_Config.m_MaxUploadSizeInBytes)
		{
			++m_Pool->m_Statistics.m_OversizedUploadFailures;
			Fail();
			return {};
		}
		const uint64_t reservedSize = (bytes.size() + alignment - 1) / alignment * alignment;
		uint64_t offset = m_Leases.empty() ? 0 :
			(m_Leases.back().m_CursorInBytes + alignment - 1) / alignment * alignment;
		if (m_Leases.empty() || offset + reservedSize >
			m_Pool->m_Pages[m_Leases.back().m_PageIndex].m_CapacityInBytes)
		{
			if (m_Leases.size() >= m_Pool->m_Config.m_MaxPagesPerBatch)
			{
				++m_Pool->m_Statistics.m_PageCountFailures;
				GGLAB_LOG_GRAPHICS_ERROR("Upload page allocation exhausted its budget.");
				Fail();
				return {};
			}
			const size_t index = m_Pool->AcquirePage(reservedSize,
				m_Pool->m_Config.m_MaxBytesPerBatch - m_Statistics.m_LeasedBytes);
			if (index == InvalidPage)
			{
				// AcquirePage logged the budget, creation or mapping failure.
				Fail();
				return {};
			}
			m_Leases.push_back({ index, 0 });
			m_Statistics.m_LeasedBytes += m_Pool->m_Pages[index].m_CapacityInBytes;
			offset = 0;
		}
		auto& lease = m_Leases.back();
		const auto& page = m_Pool->m_Pages[lease.m_PageIndex];
		const uint64_t consumed = offset + reservedSize - lease.m_CursorInBytes;
		std::memset(page.m_MappedData + lease.m_CursorInBytes, 0, consumed);
		std::memcpy(page.m_MappedData + offset, bytes.data(), bytes.size());
		lease.m_CursorInBytes = offset + reservedSize;
		++m_Statistics.m_UploadCount;
		m_Statistics.m_RequestedBytes += bytes.size();
		m_Statistics.m_ReservedBytes += consumed;
		m_Statistics.m_PageCount = m_Leases.size();
		auto& poolStatistics = m_Pool->m_Statistics;
		poolStatistics.m_HighWaterReservedBytesPerBatch = std::max(
			poolStatistics.m_HighWaterReservedBytesPerBatch, m_Statistics.m_ReservedBytes);
		poolStatistics.m_HighWaterLeasedBytesPerBatch = std::max(
			poolStatistics.m_HighWaterLeasedBytesPerBatch, m_Statistics.m_LeasedBytes);
		poolStatistics.m_HighWaterPagesPerBatch = std::max(
			poolStatistics.m_HighWaterPagesPerBatch, m_Statistics.m_PageCount);
		return { page.m_Buffer, offset, static_cast<uint32_t>(bytes.size()), m_Id };
	}

	bool RHIUploadBatch::Owns(const RHIConstantBufferBinding& binding) const noexcept
	{
		if (m_State != State::Recording || m_Failed || binding.m_UploadBatchId != m_Id)
		{
			return false;
		}
		return std::ranges::any_of(m_Leases, [&](const PageLease& lease)
			{
				return binding.m_Buffer == m_Pool->m_Pages[lease.m_PageIndex].m_Buffer &&
					binding.m_OffsetInBytes < lease.m_CursorInBytes && binding.m_SizeInBytes != 0 &&
					binding.m_SizeInBytes <= lease.m_CursorInBytes - binding.m_OffsetInBytes;
			});
	}

	bool RHIUploadBatch::Owns(const RHIReadOnlyBufferBinding& binding) const noexcept
	{
		return IsReadOnlyBufferBindingInRange(binding, UINT64_MAX, m_Pool->m_Device->GetUploadLimits()) &&
			Owns(RHIConstantBufferBinding{ binding.m_Buffer, binding.m_OffsetInBytes,
				binding.m_SizeInBytes, binding.m_UploadBatchId });
	}

	bool RHIUploadBatch::IsUploadStorage(RHIBufferHandle buffer) const noexcept
	{
		return m_Pool->m_Device->IsUploadStorage(buffer);
	}

	void RHIUploadBatch::FinishStatistics() noexcept
	{
		m_Pool->m_Statistics.m_LastBatch = m_Statistics;
	}

	bool RHIUploadBatch::Publish() noexcept
	{
		if (m_State != State::Recording)
		{
			return m_State == State::Published && !m_Failed;
		}
		for (const auto& lease : m_Leases)
		{
			if (!m_Pool->m_Device->FlushMappedBufferWrites(
				m_Pool->m_Pages[lease.m_PageIndex].m_Buffer, { 0, lease.m_CursorInBytes }))
			{
				GGLAB_LOG_GRAPHICS_ERROR("Upload write publication failed.");
				++m_Pool->m_Statistics.m_PublicationFailures;
				Fail();
			}
		}
		m_State = State::Published;
		return !m_Failed;
	}

	bool RHIUploadBatch::Submitted(const RHIFencePoint& fence) noexcept
	{
		if (m_State != State::Published || m_Failed || !fence.IsValid())
		{
			return false;
		}
		for (const auto& lease : m_Leases)
		{
			auto& page = m_Pool->m_Pages[lease.m_PageIndex];
			m_Pool->m_Device->RecordBufferUse(page.m_Buffer, fence);
			page.m_Fence = fence;
			page.m_Leased = false;
		}
		if (!m_RetainedResources.empty())
		{
			m_Pool->m_RetainedResources.push_back({ std::move(m_RetainedResources), fence, false });
		}
		m_State = State::Submitted;
		++m_Pool->m_Statistics.m_SubmittedBatchCount;
		FinishStatistics();
		return true;
	}

	void RHIUploadBatch::SubmittedWithoutCompletionFence() noexcept
	{
		GGLAB_ASSERT_MSG(m_State == State::Recording || m_State == State::Published,
			"Only an unresolved batch can be quarantined.");
		if (m_State != State::Recording && m_State != State::Published)
		{
			return;
		}
		for (const auto& lease : m_Leases)
		{
			auto& page = m_Pool->m_Pages[lease.m_PageIndex];
			page.m_Quarantined = true;
			page.m_Leased = false;
		}
		if (!m_RetainedResources.empty())
		{
			m_Pool->m_RetainedResources.push_back({ std::move(m_RetainedResources), {}, true });
		}
		m_State = State::Quarantined;
		++m_Pool->m_Statistics.m_SubmittedBatchCount;
		FinishStatistics();
	}

	void RHIUploadBatch::Cancel() noexcept
	{
		// Accepted work may already be reading the pages; it needs a fence or quarantine.
		GGLAB_ASSERT_MSG(!m_HasAcceptedSubmission,
			"Uploads read by an accepted submission cannot be cancelled.");
		if (m_HasAcceptedSubmission)
		{
			// The assertion is compiled out of release builds, where releasing the leases
			// would let a later batch rewrite pages the GPU may still read.
			GGLAB_LOG_GRAPHICS_ERROR_ALWAYS(
				"An upload batch with an accepted submission was cancelled; its pages are quarantined.");
			SubmittedWithoutCompletionFence();
			return;
		}
		GGLAB_ASSERT_MSG(m_State == State::Recording || m_State == State::Published,
			"Submitted uploads must wait for GPU completion.");
		if (m_State != State::Recording && m_State != State::Published)
		{
			return;
		}
		// The discarded writes need no publication; the pages stay mapped for reuse.
		for (const auto& lease : m_Leases)
		{
			m_Pool->m_Pages[lease.m_PageIndex].m_Leased = false;
		}
		m_RetainedResources.clear();
		m_State = State::Cancelled;
		++m_Pool->m_Statistics.m_CancelledBatchCount;
		FinishStatistics();
	}

	bool RHIUploadBatch::RetainForSubmission(std::shared_ptr<void> resource) noexcept
	{
		if (!resource || !AcceptsUploadOnCurrentThread())
		{
			Fail();
			return false;
		}
		m_RetainedResources.push_back(std::move(resource));
		return true;
	}
}
