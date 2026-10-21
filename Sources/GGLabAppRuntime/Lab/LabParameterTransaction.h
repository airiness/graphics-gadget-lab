#pragma once
#include "Lab/LabParameter.h"
#include "Lab/LabTypes.h"
#include "Parameters/ParameterEffectReceipt.h"

#include <cstdint>
#include <string>
#include <vector>

namespace gglab
{
	enum class LabParameterBatchState : uint8_t { Queued, Applying, Finished, Failed };

	struct LabParameterBatchStatus
	{
		uint64_t m_BatchId = 0;
		// Assigned at the safe apply boundary; NoChange keeps the current revision.
		uint64_t m_Revision = 0;
		LabId m_Lab;
		LabParameterBatchState m_State = LabParameterBatchState::Queued;
		// Owned admission values remain queryable before application or after session replacement.
		std::vector<LabParameterValue> m_Values;
		std::vector<ParameterEffectSnapshot> m_Items;
		std::string m_Failure;

		[[nodiscard]] bool IsTerminal() const noexcept
		{
			return m_State == LabParameterBatchState::Finished ||
				m_State == LabParameterBatchState::Failed;
		}
	};
}
