#include "GGLabRuntime/Graphics/RenderGraph/RGObjectArena.h"

#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <new>

namespace gglab
{
	struct RGObjectArena::Block
	{
		Block* m_Next = nullptr;
		std::byte* m_Payload = nullptr;
		size_t m_Capacity = 0;
		size_t m_Used = 0;
		size_t m_AllocationAlignment = 0;
	};

	namespace
	{
		[[noreturn]] void FailObjectArena(const char* reason) noexcept
		{
			// Allocation/lifetime failures must not depend on assertions or allocate through a sink.
			std::fprintf(stderr, "RGObjectArena contract failure: %s\n", reason);
			std::fflush(stderr);
			std::abort();
		}

		size_t AddChecked(size_t left, size_t right) noexcept
		{
			if (right > std::numeric_limits<size_t>::max() - left)
			{
				FailObjectArena("size overflow");
			}
			return left + right;
		}

		size_t AlignChecked(size_t value, size_t alignment) noexcept
		{
			return AddChecked(value, alignment - 1) & ~(alignment - 1);
		}
	}

	RGObjectArena::RGObjectArena() noexcept : RGObjectArena(CreateInfo{}) {}

	RGObjectArena::RGObjectArena(const CreateInfo& createInfo) noexcept : m_CreateInfo(createInfo)
	{
		if (createInfo.m_RegularBlockPayloadBytes < sizeof(DestructionRecord) ||
			createInfo.m_RegularBlockPayloadBytes % alignof(std::max_align_t) != 0)
		{
			FailObjectArena("invalid regular block payload size");
		}
		AddChecked(AlignChecked(sizeof(Block), alignof(std::max_align_t)),
			createInfo.m_RegularBlockPayloadBytes);
	}

	RGObjectArena::~RGObjectArena() noexcept
	{
		m_Destroying = true;
		while (m_LastDestructor)
		{
			DestructionRecord* record = m_LastDestructor;
			m_LastDestructor = record->m_Previous;
			record->m_Destroy(record->m_Object);
		}
		// Destructors can still borrow other live CPU payloads: release no block early.
		while (m_Blocks)
		{
			Block* block = m_Blocks;
			m_Blocks = block->m_Next;
			const size_t alignment = block->m_AllocationAlignment;
			block->~Block();
			::operator delete(block, std::align_val_t{ alignment });
		}
	}

	RGObjectArena::Block* RGObjectArena::AllocateBlock(
		size_t capacity, size_t alignment, bool dedicated) noexcept
	{
		alignment = std::max({ alignment, alignof(Block), alignof(std::max_align_t) });
		const size_t payloadOffset = AlignChecked(sizeof(Block), alignment);
		const size_t allocationBytes = AddChecked(payloadOffset, capacity);
		if (m_AllocationsUntilFailure == 0)
		{
			FailObjectArena("allocation failed");
		}
		if (m_AllocationsUntilFailure != std::numeric_limits<size_t>::max())
		{
			--m_AllocationsUntilFailure;
		}
		void* allocation = ::operator new(allocationBytes, std::align_val_t{ alignment }, std::nothrow);
		if (!allocation)
		{
			FailObjectArena("allocation failed");
		}
		Block* block = ::new (allocation) Block{
			.m_Next = m_Blocks,
			.m_Payload = static_cast<std::byte*>(allocation) + payloadOffset,
			.m_Capacity = capacity,
			.m_AllocationAlignment = alignment,
		};
		m_Blocks = block;
		m_Statistics.m_ReservedPayloadBytes = AddChecked(m_Statistics.m_ReservedPayloadBytes, capacity);
		m_Statistics.m_BlockOverheadBytes = AddChecked(m_Statistics.m_BlockOverheadBytes, payloadOffset);
		AddChecked(m_Statistics.m_ReservedPayloadBytes, m_Statistics.m_BlockOverheadBytes);
		if (dedicated)
		{
			m_Statistics.m_DedicatedBlockCount = AddChecked(m_Statistics.m_DedicatedBlockCount, 1);
		}
		else
		{
			m_Statistics.m_RegularBlockCount = AddChecked(m_Statistics.m_RegularBlockCount, 1);
		}
		return block;
	}

	void* RGObjectArena::AllocateBytes(size_t size, size_t alignment) noexcept
	{
		if (m_Destroying)
		{
			FailObjectArena("allocation during destruction");
		}
		if (size == 0)
		{
			FailObjectArena("invalid object size");
		}
		if (alignment == 0 || (alignment & (alignment - 1)) != 0)
		{
			FailObjectArena("invalid alignment");
		}
		Block* block = nullptr;
		size_t offset = 0;
		if (size > m_CreateInfo.m_RegularBlockPayloadBytes || alignment > alignof(std::max_align_t))
		{
			block = AllocateBlock(size, alignment, true);
		}
		else
		{
			block = m_CurrentRegularBlock;
			if (block)
			{
				offset = AlignChecked(block->m_Used, alignment);
			}
			if (!block || offset > block->m_Capacity || size > block->m_Capacity - offset)
			{
				block = AllocateBlock(m_CreateInfo.m_RegularBlockPayloadBytes,
					alignof(std::max_align_t), false);
				m_CurrentRegularBlock = block;
				offset = 0;
			}
		}
		const size_t consumed = AddChecked(offset - block->m_Used, size);
		m_Statistics.m_UsedPayloadBytes = AddChecked(m_Statistics.m_UsedPayloadBytes, consumed);
		block->m_Used = AddChecked(offset, size);
		return block->m_Payload + offset;
	}

	void RGObjectArena::RegisterObject(void* object, size_t size,
		DestructionRecord* record, Destructor destroy) noexcept
	{
		m_Statistics.m_ObjectBytes = AddChecked(m_Statistics.m_ObjectBytes, size);
		m_Statistics.m_ObjectCount = AddChecked(m_Statistics.m_ObjectCount, 1);
		if (record)
		{
			::new (record) DestructionRecord{ object, destroy, m_LastDestructor };
			m_LastDestructor = record;
			m_Statistics.m_DestructorRecordBytes =
				AddChecked(m_Statistics.m_DestructorRecordBytes, sizeof(DestructionRecord));
			m_Statistics.m_DestructorRecordCount = AddChecked(m_Statistics.m_DestructorRecordCount, 1);
		}
	}
}
