#include "Precompiled.h"
#include "DX12DescriptorFreeListAllocator.h"
#include "DX12DescriptorTypes.h"
#include "DX12DescriptorHeap.h"

namespace gglab
{
	DX12DescriptorFreeListAllocator::DX12DescriptorFreeListAllocator(const CreateInfo& createInfo) noexcept :
		DX12DescriptorAllocatorBase(createInfo),
		m_Allocator(createInfo.m_Range.m_Count),
		m_Generation(createInfo.m_Range.m_Count, 1)
	{
	}

	DX12DescriptorHandle DX12DescriptorFreeListAllocator::AllocateHandle(uint32_t count) noexcept
	{
		std::lock_guard lock(m_Mutex);

		FreeCompleted();

		const auto local = m_Allocator.Allocate(count);
		if (!local.IsValid())
		{
			return {};
		}

		const auto localSpan = ToSpan(local);
		const auto globalSpan = ToGlobalSpan(localSpan);
		const auto generation = (count == 1) ? m_Generation[localSpan.m_Index] : 0;

		return CreateHandleFromGlobalSpan(globalSpan, generation);
	}

	DX12DescriptorView DX12DescriptorFreeListAllocator::AllocateView(uint32_t count) noexcept
	{
		std::lock_guard lock(m_Mutex);

		FreeCompleted();

		const auto local = m_Allocator.Allocate(count);
		if (!local.IsValid())
		{
			return {};
		}

		const auto localSpan = ToSpan(local);
		const auto [globalIndex, globalCount] = ToGlobalSpan(localSpan);

		return {
			CpuHandleAtGlobalIndex(globalIndex),
			IsShaderVisible() ? GpuHandleAtGlobalIndex(globalIndex) : CD3DX12_GPU_DESCRIPTOR_HANDLE{}
		};
	}










	DX12DescriptorHandle DX12DescriptorFreeListAllocator::Allocate(uint32_t count) noexcept
	{
		std::lock_guard lock(m_Mutex);

		FreeCompleted();

		return CreateDescriptor(m_Allocator.Allocate(count));
	}

	void DX12DescriptorFreeListAllocator::Free(DX12DescriptorHandle& descriptor) noexcept
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

	void DX12DescriptorFreeListAllocator::Retire(DX12DescriptorHandle& descriptor, const DX12FencePoint& fencePoint) noexcept
	{
		RetireImpl(descriptor, fencePoint);
		descriptor.Reset();
	}

	void DX12DescriptorFreeListAllocator::FreeInFrame(DX12DescriptorHandle& descriptor) noexcept
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
				m_Pending.emplace_back(Pending{ span, fencePoint });
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
		const auto cpuStart = heap->CpuHandleStart();
		const auto gpuStart = heap->GpuHandleStart();

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
				const auto cpuStart = heap->CpuHandleStart().ptr;
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

	void DX12DescriptorFreeListAllocator::FreeHandleInternal(DX12DescriptorHandle& descriptor) noexcept
	{
		Free(descriptor);
	}

	void DX12DescriptorFreeListAllocator::RetireDescriptorInternal(const DX12DescriptorHandle& descriptor, const DX12FencePoint& fencePoint) noexcept
	{
		RetireImpl(descriptor, fencePoint);
	}

	void DX12DescriptorFreeListAllocator::FreeCompleted() noexcept
	{
		while (!m_Pending.empty())
		{
			auto& front = m_Pending.front();
			if (!front.m_FencePoint.IsCompleted())
			{
				break;
			}

			m_Allocator.Free(front.m_Span);
			m_Pending.pop_front();
		}
	}

	void DX12DescriptorFreeListAllocator::RetireImpl(const DX12DescriptorHandle& descriptor, 
		const DX12FencePoint& fencePoint) noexcept
	{
		std::lock_guard lock(m_Mutex);

		FreeCompleted();

		if (!descriptor.IsValid())
		{
			return;
		}
		GGLAB_ASSERT_MSG(descriptor.Owner() == this, "Descriptor does not belong to this allocator.");

		m_Pending.emplace_back(Pending{ ToIndexSpan(descriptor), fencePoint });
	}

	FreeListSpanAllocator::IndexSpan DX12DescriptorFreeListAllocator::ToIndexSpan(
		const DX12DescriptorHandle& descriptor) noexcept
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