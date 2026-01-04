#include "Precompiled.h"
#include "DX12DescriptorFreeListAllocator.h"
#include "DX12Descriptor.h"

namespace gglab
{
	DX12DescriptorFreeListAllocator::DX12DescriptorFreeListAllocator(
		const DX12DescriptorAllocatorBase::CreateInfo& createInfo) noexcept :
		DX12DescriptorAllocatorBase(createInfo),
		m_Allocator(GetHeap()->DescriptorCount())
	{
	}

	DX12Descriptor DX12DescriptorFreeListAllocator::Allocate(uint32_t count) noexcept
	{
		std::lock_guard lock(m_Mutex);

		FreeCompleted();

		return CreateDescriptor(m_Allocator.Allocate(count));
	}

	void DX12DescriptorFreeListAllocator::Free(DX12Descriptor& descriptor) noexcept
	{
		std::lock_guard lock(m_Mutex);

		FreeCompleted();

		if (!descriptor.IsValid())
		{
			return;
		}
		GGLAB_ASSERT_MSG(descriptor.Owner() == this, "This Descriptor do not belong this Allocator.");

		const auto span = ToIndexSpan(descriptor);
		m_Allocator.Free(span);
		descriptor.Reset();
	}

	void DX12DescriptorFreeListAllocator::Retire(DX12Descriptor& descriptor, const DX12FencePoint& fencePoint) noexcept
	{
		RetireImpl(descriptor, fencePoint);
		descriptor.Reset();
	}

	void DX12DescriptorFreeListAllocator::FreeInFrame(DX12Descriptor& descriptor) noexcept
	{
		std::lock_guard lock(m_Mutex);
	
		if (!descriptor.IsValid())
		{
			return;
		}
		GGLAB_ASSERT_MSG(descriptor.Owner() == this, "Descriptor does not belong to this allocator.");

		m_FreeInFrameSpans.emplace_back(ToIndexSpan(descriptor));
		descriptor.Reset();
	}

	void DX12DescriptorFreeListAllocator::EndFrame(const DX12FencePoint& fencePoint) noexcept
	{
		std::lock_guard lock(m_Mutex);

		if (!m_FreeInFrameSpans.empty())
		{
			FreeCompleted();

			for (const auto& span : m_FreeInFrameSpans)
			{
				m_Pendings.emplace_back(Pending{ span, fencePoint });
			}

			m_FreeInFrameSpans.clear();
		}

		FreeCompleted();
	}

	DX12DescriptorView DX12DescriptorFreeListAllocator::AllocateRaw() noexcept
	{
		std::lock_guard lock(m_Mutex);

		FreeCompleted();

		const auto span = m_Allocator.Allocate(1);
		GGLAB_ASSERT_MSG(span.IsValid(), "Allocate failed.");

		auto* heap = GetHeap();
		const auto incrementSize = heap->DescriptorIncrementSize();
		const auto cpuStart = heap->CpuStart();
		const auto gpuStart = heap->GpuStart();

		CD3DX12_CPU_DESCRIPTOR_HANDLE cpuHandle{};
		cpuHandle.InitOffsetted(cpuStart, span.m_Index, incrementSize);

		CD3DX12_GPU_DESCRIPTOR_HANDLE gpuHandle{};
		gpuHandle.InitOffsetted(gpuStart, span.m_Index, incrementSize);

		return DX12DescriptorView{ .m_CpuHandle = cpuHandle, .m_GpuHandle = gpuHandle };
	}

	void DX12DescriptorFreeListAllocator::FreeRaw(DX12DescriptorView view) noexcept
	{
		const auto computeIndex = [this](CD3DX12_CPU_DESCRIPTOR_HANDLE cpuHandle)
			{
				auto* heap = GetHeap();
				const auto cpuStart = heap->CpuStart().ptr;
				const auto incrementSize = heap->DescriptorIncrementSize();

				GGLAB_ASSERT_MSG(cpuHandle.ptr >= cpuStart, "CPU handle not from this heap.");
				const uint64_t delta = cpuHandle.ptr - cpuStart;
				GGLAB_ASSERT_MSG(delta % incrementSize == 0, "CPU handle not aligned.");
				const uint32_t index = static_cast<uint32_t>(delta / incrementSize);
				GGLAB_ASSERT_MSG(index < GetHeap()->DescriptorCount(), "CPU handle out of range.");

				return index;
			};

		std::lock_guard lock(m_Mutex);

		const auto index = computeIndex(view.m_CpuHandle);
		m_FreeInFrameSpans.emplace_back(ToIndexSpan(index, 1));
	}

	void DX12DescriptorFreeListAllocator::FreeDescriptorInternal(DX12Descriptor& descriptor) noexcept
	{
		Free(descriptor);
	}

	void DX12DescriptorFreeListAllocator::RetireDescriptorInternal(const DX12Descriptor& descriptor, const DX12FencePoint& fencePoint) noexcept
	{
		RetireImpl(descriptor, fencePoint);
	}

	void DX12DescriptorFreeListAllocator::FreeCompleted() noexcept
	{
		while (!m_Pendings.empty())
		{
			auto& front = m_Pendings.front();
			if (!front.m_FencePoint.IsCompleted())
			{
				break;
			}

			m_Allocator.Free(front.m_Span);
			m_Pendings.pop_front();
		}
	}

	void DX12DescriptorFreeListAllocator::RetireImpl(const DX12Descriptor& descriptor, 
		const DX12FencePoint& fencePoint) noexcept
	{
		std::lock_guard lock(m_Mutex);

		FreeCompleted();

		if (!descriptor.IsValid())
		{
			return;
		}
		GGLAB_ASSERT_MSG(descriptor.Owner() == this, "Descriptor does not belong to this allocator.");

		m_Pendings.emplace_back(Pending{ ToIndexSpan(descriptor), fencePoint });
	}

	FreeListSpanAllocator::IndexSpan DX12DescriptorFreeListAllocator::ToIndexSpan(
		const DX12Descriptor& descriptor) noexcept
	{
		return FreeListSpanAllocator::IndexSpan
		{
			.m_Index = descriptor.Index(),
			.m_Count = descriptor.Count()
		};
	}

	FreeListSpanAllocator::IndexSpan DX12DescriptorFreeListAllocator::ToIndexSpan(
		uint32_t index, uint32_t count) noexcept
	{
		return FreeListSpanAllocator::IndexSpan
		{
			.m_Index = index,
			.m_Count = count
		};
	}
}