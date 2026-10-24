#pragma once
#include "Lab/LabParameter.h"
#include "Parameters/ParameterEffectReceipt.h"

#include <cstdint>
#include <string>
#include <vector>

namespace gglab
{
	// Zero is the external session client. Internal owners use their stable nonzero identity.
	class ParameterBatchOwner final
	{
	public:
		constexpr ParameterBatchOwner() noexcept = default;
		explicit constexpr ParameterBatchOwner(uint64_t identity) noexcept : m_Identity(identity) {}
		[[nodiscard]] bool IsExternal() const noexcept { return m_Identity == 0; }
		bool operator==(const ParameterBatchOwner&) const noexcept = default;

	private:
		uint64_t m_Identity = 0;
	};

	enum class ParameterBatchState : uint8_t
	{
		Queued,
		Applying,
		Finished,
		Failed,
	};

	struct ParameterBatchStatus
	{
		uint64_t m_BatchId = 0;
		// Assigned at the safe apply boundary; NoChange keeps the current revision.
		uint64_t m_Revision = 0;
		std::string m_ContentId;
		std::string m_Source = "lab";
		ParameterBatchState m_State = ParameterBatchState::Queued;
		LabChangeImpact m_Impact = LabChangeImpact::Immediate;
		// Owned admission values remain queryable before application or after session replacement.
		std::vector<LabParameterValue> m_Values;
		std::vector<ParameterEffectSnapshot> m_Items;
		std::string m_Failure;

		[[nodiscard]] bool IsTerminal() const noexcept
		{
			return m_State == ParameterBatchState::Finished ||
				m_State == ParameterBatchState::Failed;
		}
	};
}
