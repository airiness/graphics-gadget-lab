#pragma once

namespace gglab
{
	class RGArenaAllocator
	{
	private:
		// Destructor Entry
		struct DtorEntry
		{
			void* m_Ptr = nullptr;
			void (*m_Fn)(void*) noexcept;
		};

	public:
		explicit RGArenaAllocator(size_t initCapacity = 1u << 20) noexcept;
		~RGArenaAllocator() noexcept;
		GGLAB_DELETE_COPYABLE(RGArenaAllocator);
		RGArenaAllocator(RGArenaAllocator&& other) noexcept;
		RGArenaAllocator& operator=(RGArenaAllocator&& other) noexcept;

		template<typename T, typename... ARGS>
		T* Make(ARGS&&... args) noexcept;

		template<typename T, typename... ARGS>
		T* MakeTracked(ARGS&&... args) noexcept;

		void Reset() noexcept;

		size_t Size() const noexcept;
		size_t Capacity() const noexcept;

	private:
		void* AllocateBytes(size_t size, size_t alignment = alignof(std::max_align_t)) noexcept;
		void Grow(size_t minAdditional) noexcept;
		void RunDtors() noexcept;
		void Exchange(RGArenaAllocator&& other) noexcept;

		static std::byte* AlignUp(std::byte* pointer, size_t alignment) noexcept;

	private:
		std::byte* m_Base = nullptr;
		std::byte* m_Head = nullptr;

		size_t m_Capacity = 0;

		std::vector<DtorEntry> m_DtorEntries;
	};

	template<typename T, typename... ARGS>
	inline T* RGArenaAllocator::Make(ARGS&&...args) noexcept
	{
		void* mem = AllocateBytes(sizeof(T), alignof(T));

		// Placement new
		return ::new (mem) T(std::forward<ARGS>(args)...);
	}

	template<typename T, typename... ARGS>
	inline T* RGArenaAllocator::MakeTracked(ARGS&&... args) noexcept
	{
		void* mem = AllocateBytes(sizeof(T), alignof(T));
		T* obj = ::new (mem) T(std::forward<ARGS>(args)...);

		// If T type is not trivial, make sure call it's destructor when released.
		if constexpr (!std::is_trivially_destructible_v<T>)
		{
			m_DtorEntries.push_back(
				DtorEntry{ obj, [](void* p) noexcept { static_cast<T*>(p)->~T(); } });
		}

		return obj;
	}
}