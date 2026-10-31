#pragma once
#include "GGLabRuntime/Graphics/RHI/RHIAccelerationStructure.h"
#include "GGLabRuntime/Graphics/RHI/RHIResource.h"
#include "Graphics/RHI/RHIHandleTable.h"

#include <memory>
#include <vector>
#include <utility>

namespace gglab
{
	struct RHIAccelerationStructureBackingBase
	{
		virtual ~RHIAccelerationStructureBackingBase() = default;
		RHIAccelerationStructureType m_Type = RHIAccelerationStructureType::BottomLevel;
		uint64_t m_SizeInBytes = 0;
		RHIBufferOwner m_Storage;
		// The BLAS set is immutable for one TLAS lifetime. A topology replacement gets
		// a new TLAS; recorded and aborted builds cannot drop its prior native links.
		std::vector<std::shared_ptr<RHIAccelerationStructureBackingBase>> m_BottomLevels;
	};

	class RHIAccelerationStructureStore
	{
	public:
		[[nodiscard]] RHIAccelerationStructureHandle Insert(
			std::shared_ptr<RHIAccelerationStructureBackingBase> resource) noexcept
		{
			const auto handle = m_Slots.Allocate();
			m_Slots.SlotAt(handle.Index()).m_Resource = std::move(resource);
			return handle;
		}
		[[nodiscard]] std::shared_ptr<RHIAccelerationStructureBackingBase> Resolve(
			RHIAccelerationStructureHandle handle) const noexcept
		{
			const auto* slot = m_Slots.Resolve(handle);
			return slot ? slot->m_Resource : nullptr;
		}
		[[nodiscard]] bool Destroy(RHIAccelerationStructureHandle handle) noexcept
		{
			if (m_Slots.BeginRetirement(handle) != RHIHandleValidationResult::Valid)
			{
				return false;
			}
			m_Slots.SlotAt(handle.Index()).m_Resource.reset();
			m_Slots.Retire(handle.Index());
			return true;
		}
		void Clear() noexcept { m_Slots.Clear(); }

	private:
		struct Slot
		{
			RHIAccelerationStructureHandle::GenerationType m_Generation = 1;
			RHIHandleSlotState m_State = RHIHandleSlotState::Free;
			std::shared_ptr<RHIAccelerationStructureBackingBase> m_Resource;
		};
		RHIHandleTable<RHIAccelerationStructureHandle, Slot> m_Slots;
	};
}
