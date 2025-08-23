#include "Precompiled.h"
#include "RGArenaAllocator.h"

namespace gglab
{
	RGArenaAllocator::RGArenaAllocator(size_t initCapacity) noexcept
		: m_Capacity(initCapacity)
	{
		m_Base = static_cast<std::byte*>(std::malloc(m_Capacity));
		GGLAB_ASSERT_MSG(m_Base, "Render Graph Arena Allocator create allocation failed.");

		m_Head = m_Base;
	}

	RGArenaAllocator::~RGArenaAllocator() noexcept
	{
		RunDtors();

		std::free(m_Base);
	}

	RGArenaAllocator::RGArenaAllocator(RGArenaAllocator&& other) noexcept
	{
		Exchange(std::move(other));
	}

	RGArenaAllocator& RGArenaAllocator::operator=(RGArenaAllocator&& other) noexcept
	{
		if (this != &other)
		{
			this->~RGArenaAllocator();
			Exchange(std::move(other));
		}

		return *this;
	}

	void RGArenaAllocator::Reset() noexcept
	{
		RunDtors();
		m_DtorEntries.clear();
		m_Head = m_Base;
	}

	size_t RGArenaAllocator::Size() const noexcept
	{
		return static_cast<size_t>(m_Head - m_Base);
	}

	size_t RGArenaAllocator::Capacity() const noexcept
	{
		return m_Capacity;
	}

	void* RGArenaAllocator::AllocateBytes(size_t size, size_t alignment) noexcept
	{
		GGLAB_ASSERT_MSG((alignment & (alignment - 1)) == 0, "alignment must be power of 2.");

		std::byte* aligned = AlignUp(m_Head, alignment);
		size_t used = static_cast<size_t>(aligned - m_Base) + size;
		if (used > m_Capacity)
		{
			Grow(size + static_cast<size_t>(aligned - m_Base));
			aligned = AlignUp(m_Head, alignment);
		}

		m_Head = aligned + size;
		return aligned;
	}

	void RGArenaAllocator::Grow(size_t minAdditional) noexcept
	{
		size_t used = Size();

		size_t newCapacity = m_Capacity ? m_Capacity * 2 : minAdditional;
		while (newCapacity < minAdditional + used)
		{
			newCapacity *= 2;
		}

		std::byte* newBase = static_cast<std::byte*>(std::malloc(newCapacity));
		GGLAB_ASSERT_MSG(newBase, "Render Graph Arena Allocator grow allocation failed.");

		// copy data allocated.
		std::memcpy(newBase, m_Base, used);

		std::ptrdiff_t delta = newBase - m_Base;
		for (auto& dtor : m_DtorEntries)
		{
			dtor.m_Ptr = static_cast<std::byte*>(dtor.m_Ptr) + delta;
		}

		std::free(m_Base);
		m_Base = newBase;
		m_Head = m_Base + used;
		m_Capacity = newCapacity;
	}

	void RGArenaAllocator::RunDtors() noexcept
	{
		for (auto iter = m_DtorEntries.rbegin(); iter != m_DtorEntries.rend(); ++iter)
		{
			iter->m_Fn(iter->m_Ptr);
		}
	}

	void RGArenaAllocator::Exchange(RGArenaAllocator&& other) noexcept
	{
		m_Base = std::exchange(other.m_Base, nullptr);
		m_Head = std::exchange(other.m_Head, nullptr);
		m_Capacity = std::exchange(other.m_Capacity, 0);
		m_DtorEntries = std::move(other.m_DtorEntries);
	}

	std::byte* RGArenaAllocator::AlignUp(std::byte* pointer, size_t alignment) noexcept
	{
		auto uintPointer = reinterpret_cast<std::uintptr_t>(pointer);
		auto aligned = (uintPointer + (alignment - 1)) & ~(alignment - 1);
		return reinterpret_cast<std::byte*>(aligned);
	}
}


