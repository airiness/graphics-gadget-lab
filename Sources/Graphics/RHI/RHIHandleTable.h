#pragma once
#include "Core/CoreMacros.h"

#include <cstdint>
#include <utility>
#include <vector>

namespace gglab
{
	enum class RHIHandleSlotState : uint8_t
	{
		Free,
		Alive,
		PendingRetirement,
	};

	enum class RHIHandleValidationResult : uint8_t
	{
		Valid,
		Invalid,
		Stale,
		DoubleDestroy,
		NonLive,
	};

	template<typename Handle, typename Slot>
	class RHIHandleTable
	{
	public:
		using IndexType = typename Handle::IndexType;
		using GenerationType = typename Handle::GenerationType;

		static GenerationType NextGeneration(GenerationType generation) noexcept
		{
			++generation;
			if (generation == Handle::InvalidGeneration)
			{
				++generation;
			}

			return generation;
		}

		Handle Allocate() noexcept
		{
			IndexType slotIndex = 0;
			if (!m_FreeSlots.empty())
			{
				slotIndex = m_FreeSlots.back();
				m_FreeSlots.pop_back();
			}
			else
			{
				slotIndex = static_cast<IndexType>(m_Slots.size());
				m_Slots.emplace_back();
			}

			Slot& slot = m_Slots[slotIndex];
			GGLAB_ASSERT_MSG(slot.m_State == RHIHandleSlotState::Free, "Allocated RHI handle slot is not free.");
			slot.m_State = RHIHandleSlotState::Alive;
			return Handle(slotIndex, slot.m_Generation);
		}

		RHIHandleValidationResult BeginRetirement(Handle handle) noexcept
		{
			if (!handle.IsValid() || handle.Index() >= m_Slots.size())
			{
				return RHIHandleValidationResult::Invalid;
			}

			Slot& slot = m_Slots[handle.Index()];
			if (slot.m_Generation != handle.Generation())
			{
				if (slot.m_State == RHIHandleSlotState::PendingRetirement &&
					slot.m_Generation == NextGeneration(handle.Generation()))
				{
					return RHIHandleValidationResult::DoubleDestroy;
				}

				return RHIHandleValidationResult::Stale;
			}

			if (slot.m_State != RHIHandleSlotState::Alive)
			{
				return RHIHandleValidationResult::NonLive;
			}

			slot.m_State = RHIHandleSlotState::PendingRetirement;
			slot.m_Generation = NextGeneration(slot.m_Generation);
			return RHIHandleValidationResult::Valid;
		}

		void Retire(IndexType slotIndex) noexcept
		{
			GGLAB_ASSERT_MSG(slotIndex < m_Slots.size(), "RHI handle slot index is out of range.");

			Slot& slot = m_Slots[slotIndex];
			GGLAB_ASSERT_MSG(slot.m_State == RHIHandleSlotState::PendingRetirement, "Retired RHI handle slot is not pending retirement.");
			slot.m_State = RHIHandleSlotState::Free;
			m_FreeSlots.push_back(slotIndex);
		}

		[[nodiscard]] bool IsAlive(Handle handle) const noexcept
		{
			const Slot* slot = Resolve(handle);
			return slot != nullptr;
		}

		[[nodiscard]] Slot* Resolve(Handle handle) noexcept
		{
			return const_cast<Slot*>(std::as_const(*this).Resolve(handle));
		}

		[[nodiscard]] const Slot* Resolve(Handle handle) const noexcept
		{
			if (!handle.IsValid() || handle.Index() >= m_Slots.size())
			{
				return nullptr;
			}

			const Slot& slot = m_Slots[handle.Index()];
			if (slot.m_State != RHIHandleSlotState::Alive ||
				slot.m_Generation != handle.Generation())
			{
				return nullptr;
			}

			return &slot;
		}

		[[nodiscard]] Slot& SlotAt(IndexType slotIndex) noexcept
		{
			GGLAB_ASSERT_MSG(slotIndex < m_Slots.size(), "RHI handle slot index is out of range.");
			return m_Slots[slotIndex];
		}

		[[nodiscard]] const Slot& SlotAt(IndexType slotIndex) const noexcept
		{
			GGLAB_ASSERT_MSG(slotIndex < m_Slots.size(), "RHI handle slot index is out of range.");
			return m_Slots[slotIndex];
		}

		[[nodiscard]] std::vector<Slot>& Slots() noexcept { return m_Slots; }
		[[nodiscard]] const std::vector<Slot>& Slots() const noexcept { return m_Slots; }
		[[nodiscard]] uint32_t Size() const noexcept { return static_cast<uint32_t>(m_Slots.size()); }

		void Clear() noexcept
		{
			m_Slots.clear();
			m_FreeSlots.clear();
		}

	private:
		std::vector<Slot> m_Slots;
		std::vector<IndexType> m_FreeSlots;
	};
}
