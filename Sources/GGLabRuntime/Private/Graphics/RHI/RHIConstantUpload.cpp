#include "GGLabRuntime/Graphics/RHI/RHIConstantUpload.h"
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

	RHIConstantUploadPool::RHIConstantUploadPool(
		RHIDevice& device, const RHIConstantUploadConfig& config) noexcept :
		m_Device(&device), m_Config(config)
	{
	}

	RHIConstantUploadPool::~RHIConstantUploadPool() noexcept
	{
		for (const auto& page : m_Pages)
		{
			GGLAB_ASSERT_MSG(!page.m_Leased,
				"Constant upload batches must be submitted or cancelled before pool destruction.");
			// Every write was published when its batch was; ending the mapping writes nothing.
			m_Device->UnmapBuffer(page.m_Buffer, {});
			m_Device->DestroyBuffer(page.m_Buffer);
		}
	}

	std::unique_ptr<RHIConstantUploadBatch> RHIConstantUploadPool::BeginBatch() noexcept
	{
		return std::unique_ptr<RHIConstantUploadBatch>(new RHIConstantUploadBatch(*this));
	}

	void RHIConstantUploadPool::ReclaimCompleted() noexcept
	{
		for (auto& page : m_Pages)
		{
			if (page.m_Fence.IsValid() && m_Device->IsFencePointCompleted(page.m_Fence))
			{
				page.m_Fence = {};
			}
		}
	}

	size_t RHIConstantUploadPool::GetPendingPageCount() const noexcept
	{
		return std::ranges::count_if(m_Pages, [](const Page& page)
			{
				return page.m_Fence.IsValid();
			});
	}

	size_t RHIConstantUploadPool::AcquirePage(uint64_t minimumSize) noexcept
	{
		ReclaimCompleted();
		for (size_t index = 0; index < m_Pages.size(); ++index)
		{
			auto& page = m_Pages[index];
			if (!page.m_Leased && !page.m_Fence.IsValid() && page.m_CapacityInBytes >= minimumSize)
			{
				page.m_Leased = true;
				return index;
			}
		}
		if (m_Pages.size() >= m_Config.m_MaxPoolPages || m_Config.m_PageSizeInBytes == 0)
		{
			GGLAB_LOG_GRAPHICS_ERROR("Constant upload page allocation exhausted its budget.");
			return InvalidPage;
		}
		const uint64_t capacity = std::max<uint64_t>(minimumSize, m_Config.m_PageSizeInBytes);
		const auto buffer = m_Device->CreateBuffer(
			{
				.m_SizeInBytes = capacity,
				.m_Usage = RHIBufferUsage::Constant,
				.m_MemoryUsage = RHIMemoryUsage::CpuToGpu,
			},
			{
				.m_Domain = RHIResourceDebugDomain::Renderer,
				.m_Label = "RHI.ConstantUpload",
			});
		if (!buffer.IsValid())
		{
			GGLAB_LOG_GRAPHICS_ERROR("Constant upload page creation failed.");
			return InvalidPage;
		}
		// Pages are mapped once and stay mapped; each batch publishes its leases by flushing.
		auto* mapped = static_cast<std::byte*>(m_Device->MapBuffer(buffer, {}));
		if (!mapped)
		{
			GGLAB_LOG_GRAPHICS_ERROR("Constant upload page mapping failed.");
			m_Device->DestroyBuffer(buffer);
			return InvalidPage;
		}
		m_Pages.push_back({ buffer, mapped, capacity, {}, true });
		return m_Pages.size() - 1;
	}

	RHIConstantUploadBatch::RHIConstantUploadBatch(RHIConstantUploadPool& pool) noexcept :
		m_Pool(&pool), m_Id(NextUploadBatchId.fetch_add(1, std::memory_order_relaxed))
	{
	}

	RHIConstantUploadBatch::~RHIConstantUploadBatch() noexcept
	{
		GGLAB_ASSERT_MSG(m_State == State::Submitted || m_State == State::Cancelled,
			"A constant upload batch requires explicit submission or command-stream cancellation.");
	}

	RHIConstantBufferBinding RHIConstantUploadBatch::Upload(std::span<const std::byte> bytes) noexcept
	{
		auto& device = *m_Pool->m_Device;
		if (m_State != State::Recording || m_Failed || bytes.empty() ||
			bytes.size() % sizeof(uint32_t) != 0 || bytes.size() > device.GetMaxConstantBufferRange())
		{
			Fail();
			return {};
		}
		const uint64_t alignment =
			std::max(1u, device.GetBufferViewAlignment(RHIBufferViewType::ConstantBuffer));
		const uint64_t reservedSize = (bytes.size() + alignment - 1) / alignment * alignment;
		if (m_Leases.empty() || m_Leases.back().m_CursorInBytes + reservedSize >
			m_Pool->m_Pages[m_Leases.back().m_PageIndex].m_CapacityInBytes)
		{
			if (m_Leases.size() >= m_Pool->m_Config.m_MaxPagesPerBatch)
			{
				GGLAB_LOG_GRAPHICS_ERROR("Constant upload page allocation exhausted its budget.");
				Fail();
				return {};
			}
			const size_t index = m_Pool->AcquirePage(reservedSize);
			if (index == InvalidPage)
			{
				// AcquirePage logged the budget, creation or mapping failure.
				Fail();
				return {};
			}
			m_Leases.push_back({ index, 0 });
		}
		auto& lease = m_Leases.back();
		const auto& page = m_Pool->m_Pages[lease.m_PageIndex];
		const uint64_t offset = lease.m_CursorInBytes;
		std::memset(page.m_MappedData + offset, 0, reservedSize);
		std::memcpy(page.m_MappedData + offset, bytes.data(), bytes.size());
		lease.m_CursorInBytes += reservedSize;
		return { page.m_Buffer, offset, static_cast<uint32_t>(bytes.size()), m_Id };
	}

	bool RHIConstantUploadBatch::Owns(const RHIConstantBufferBinding& binding) const noexcept
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

	bool RHIConstantUploadBatch::PublishWrites() noexcept
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
				GGLAB_LOG_GRAPHICS_ERROR("Constant upload write publication failed.");
				Fail();
			}
		}
		m_State = State::Published;
		return !m_Failed;
	}

	bool RHIConstantUploadBatch::Submitted(const RHIFencePoint& fence) noexcept
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
		m_State = State::Submitted;
		return true;
	}

	void RHIConstantUploadBatch::Cancel() noexcept
	{
		GGLAB_ASSERT_MSG(m_State == State::Recording || m_State == State::Published,
			"Submitted constant uploads must wait for GPU completion.");
		if (m_State != State::Recording && m_State != State::Published)
		{
			return;
		}
		// The discarded writes need no publication; the pages stay mapped for reuse.
		for (const auto& lease : m_Leases)
		{
			m_Pool->m_Pages[lease.m_PageIndex].m_Leased = false;
		}
		m_State = State::Cancelled;
	}
}
