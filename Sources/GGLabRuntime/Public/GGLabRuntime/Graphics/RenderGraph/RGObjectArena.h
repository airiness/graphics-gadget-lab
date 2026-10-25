#pragma once
#include "GGLabFoundation/Base/CoreMacros.h"

#include <cstddef>
#include <limits>
#include <new>
#include <type_traits>
#include <utility>

namespace gglab
{
	// Graph-owner-thread CPU storage. Successful Make calls never relocate objects.
	// Borrowers must end before arena destruction; payload destructors cannot reenter
	// the containing graph. GPU owners and asynchronous consumers need separate lifetimes.
	class RGObjectArena
	{
	public:
		struct CreateInfo
		{
			size_t m_RegularBlockPayloadBytes = 64u << 10;
		};

		struct Statistics
		{
			size_t m_ObjectBytes = 0;
			size_t m_ObjectCount = 0;
			size_t m_DestructorRecordBytes = 0;
			size_t m_DestructorRecordCount = 0;
			size_t m_UsedPayloadBytes = 0;
			size_t m_ReservedPayloadBytes = 0;
			size_t m_BlockOverheadBytes = 0;
			size_t m_RegularBlockCount = 0;
			size_t m_DedicatedBlockCount = 0;
		};

		RGObjectArena() noexcept;
		explicit RGObjectArena(const CreateInfo& createInfo) noexcept;
		~RGObjectArena() noexcept;
		GGLAB_DELETE_COPYABLE_MOVABLE(RGObjectArena);

		// Construction follows the graph's noexcept policy; constructor failure terminates.
		// Nested construction is allowed. Allocation during destruction fails in all builds.
		template <typename T, typename... Args> T* Make(Args&&... args) noexcept;
		// CPU-only counters. During nested construction, reservations may precede the
		// successfully constructed object/registered-destructor counts.
		[[nodiscard]] Statistics GetStatistics() const noexcept { return m_Statistics; }

	private:
		struct Block;
		using Destructor = void (*)(void*) noexcept;
		struct DestructionRecord
		{
			void* m_Object;
			Destructor m_Destroy;
			DestructionRecord* m_Previous;
		};

		void* AllocateBytes(size_t size, size_t alignment) noexcept;
		Block* AllocateBlock(size_t capacity, size_t alignment, bool dedicated) noexcept;
		void RegisterObject(void* object, size_t size, DestructionRecord* record,
			Destructor destroy) noexcept;

		Block* m_Blocks = nullptr;
		Block* m_CurrentRegularBlock = nullptr;
		DestructionRecord* m_LastDestructor = nullptr;
		CreateInfo m_CreateInfo;
		Statistics m_Statistics;
		bool m_Destroying = false;
		// Private deterministic allocation failure seam for the owning Runtime tests.
		size_t m_AllocationsUntilFailure = std::numeric_limits<size_t>::max();
		friend struct RGObjectArenaTestAccess;
	};

	template <typename T, typename... Args>
	T* RGObjectArena::Make(Args&&... args) noexcept
	{
		void* storage = AllocateBytes(sizeof(T), alignof(T));
		DestructionRecord* record = nullptr;
		Destructor destroy = nullptr;
		if constexpr (!std::is_trivially_destructible_v<T>)
		{
			static_assert(std::is_nothrow_destructible_v<T>,
				"Graph-owned objects must have non-throwing destructors.");
			record = static_cast<DestructionRecord*>(
				AllocateBytes(sizeof(DestructionRecord), alignof(DestructionRecord)));
			destroy = [](void* object) noexcept { static_cast<T*>(object)->~T(); };
		}
		T* object = ::new (storage) T(std::forward<Args>(args)...);
		// Register after construction: an outer object is destroyed before anything
		// whose construction it completed recursively. No registration allocation remains.
		RegisterObject(const_cast<std::remove_cv_t<T>*>(object), sizeof(T), record, destroy);
		return object;
	}
}
