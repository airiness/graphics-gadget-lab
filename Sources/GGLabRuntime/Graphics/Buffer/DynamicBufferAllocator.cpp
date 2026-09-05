#include "Graphics/Buffer/DynamicBufferAllocator.h"
#include "Core/Allocator/RingSpanAllocator.h"
#include "GGLabFoundation/Base/CoreMacros.h"
#include "GGLabRuntime/Core/Log/LogMacros.h"
#include "GGLabRuntime/Graphics/RHI/RHIDevice.h"

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <string_view>

namespace gglab
{
	DynamicBufferAllocator::DynamicBufferAllocator(const CreateInfo& createInfo) noexcept :
		m_Device(createInfo.m_Device),
		m_Ring(std::make_unique<RingSpanAllocator>(createInfo.m_CapacityInBytes)),
		m_CapacityInBytes(createInfo.m_CapacityInBytes), m_MemoryUsage(createInfo.m_MemoryUsage),
		m_ViewType(createInfo.m_ViewType)
	{
		GGLAB_ASSERT_MSG(m_Device != nullptr, "DynamicBufferAllocator requires an RHI device.");
		GGLAB_ASSERT_MSG(
			m_CapacityInBytes > 0, "DynamicBufferAllocator capacity must be non-zero.");
		GGLAB_ASSERT_MSG(m_MemoryUsage != RHIMemoryUsage::GpuOnly,
			"DynamicBufferAllocator requires CPU-visible memory.");

		m_AllocationAlignment = std::max(
			createInfo.m_AllocationAlignment, m_Device->GetBufferViewAlignment(m_ViewType));

		RHIBufferDesc desc{};
		desc.m_SizeInBytes = m_CapacityInBytes;
		desc.m_StrideInBytes = createInfo.m_StrideInBytes;
		desc.m_Usage = createInfo.m_Usage;
		desc.m_MemoryUsage = m_MemoryUsage;
		const RHIResourceDebugIdentityDesc debugIdentity{
			.m_Domain = RHIResourceDebugDomain::Renderer,
			.m_Category = "DynamicBuffer",
			.m_Label = createInfo.m_DebugName ? std::string_view(createInfo.m_DebugName)
											  : std::string_view("Unspecified"),
		};
		m_Buffer = RHIBufferOwner(m_Device, m_Device->CreateBuffer(desc, debugIdentity));
		GGLAB_ASSERT_MSG(m_Buffer, "DynamicBufferAllocator failed to create its RHI buffer.");

		if (m_Buffer)
		{
			const RHIMappedBufferRange readRange = m_MemoryUsage == RHIMemoryUsage::GpuToCpu ?
				RHIMappedBufferRange{ 0, m_CapacityInBytes } : RHIMappedBufferRange{};
			m_MappedData = static_cast<std::byte*>(m_Device->MapBuffer(m_Buffer.Get(), readRange));
		}
		GGLAB_ASSERT_MSG(
			m_MappedData != nullptr, "DynamicBufferAllocator failed to map its RHI buffer.");
	}

	DynamicBufferAllocator::~DynamicBufferAllocator() noexcept
	{
		Tick();
		GGLAB_ASSERT_MSG(m_PendingRetirements.empty(),
			"DynamicBufferAllocator destroyed with GPU allocations still pending retirement.");
		if (m_MappedData && m_Buffer)
		{
			const RHIMappedBufferRange writtenRange =
				m_MemoryUsage == RHIMemoryUsage::CpuToGpu ?
				RHIMappedBufferRange{ 0, m_CapacityInBytes } :
				RHIMappedBufferRange{};
			m_Device->UnmapBuffer(m_Buffer.Get(), writtenRange);
			m_MappedData = nullptr;
		}
	}

	DynamicBufferAllocation DynamicBufferAllocator::Allocate(
		uint32_t sizeInBytes, uint32_t alignment) noexcept
	{
		DynamicBufferAllocation result{};
		if (!m_MappedData || !m_Buffer)
		{
			GGLAB_LOG_GRAPHICS_WARN("DynamicBufferAllocator is not backed by a mapped buffer.");
			return result;
		}
		const uint32_t effectiveAlignment = std::max(alignment, m_AllocationAlignment);
		const auto allocation = m_Ring->AllocateAligned(sizeInBytes, effectiveAlignment);
		if (!allocation.IsValid())
		{
			GGLAB_LOG_GRAPHICS_WARN(
				"DynamicBufferAllocator allocation failed (size={}, alignment={}, capacity={}, used={}).",
				sizeInBytes, effectiveAlignment, m_CapacityInBytes, m_Ring->GetCurrentUsage());
			return result;
		}

		result.m_OffsetInBytes = allocation.m_AlignedIndex;
		result.m_SizeInBytes = sizeInBytes;
		result.m_ReservedIndex = allocation.m_ReservedSpan.m_Index;
		result.m_ReservedCount = allocation.m_ReservedSpan.m_Count;
		return result;
	}

	bool DynamicBufferAllocator::Write(
		const DynamicBufferAllocation& allocation, const void* data, uint32_t sizeInBytes) noexcept
	{
		if (!allocation.IsValid() || !data || !m_MappedData ||
			sizeInBytes > allocation.m_SizeInBytes ||
			allocation.m_OffsetInBytes + sizeInBytes > m_CapacityInBytes)
		{
			GGLAB_LOG_GRAPHICS_WARN("DynamicBufferAllocator rejected an invalid write.");
			return false;
		}

		std::memcpy(m_MappedData + allocation.m_OffsetInBytes, data, sizeInBytes);
		return true;
	}

	void DynamicBufferAllocator::Retire(
		DynamicBufferAllocation* allocation, const RHIFencePoint& fencePoint) noexcept
	{
		if (!allocation || !allocation->IsValid())
		{
			return;
		}
		GGLAB_ASSERT_MSG(fencePoint.IsValid(),
			"Dynamic buffer allocations require a valid retirement fence point.");

		const uint64_t version = m_NextRetirementVersion++;
		m_Ring->RecordRetire(
			{ allocation->m_ReservedIndex, allocation->m_ReservedCount }, version);
		m_PendingRetirements.push_back({ version, fencePoint });
		allocation->Reset();
	}

	void DynamicBufferAllocator::Tick() noexcept
	{
		while (!m_PendingRetirements.empty())
		{
			const PendingRetirement& front = m_PendingRetirements.front();
			if (!m_Device->IsFencePointCompleted(front.m_FencePoint))
			{
				break;
			}

			m_Ring->FreeCompletedVersion(front.m_Version);
			m_PendingRetirements.pop_front();
		}
	}

	uint32_t DynamicBufferAllocator::GetCurrentUsageInBytes() const noexcept
	{
		return m_Ring->GetCurrentUsage();
	}

	uint32_t DynamicBufferAllocator::GetHighWaterInBytes() const noexcept
	{
		return m_Ring->GetHighWater();
	}
}
