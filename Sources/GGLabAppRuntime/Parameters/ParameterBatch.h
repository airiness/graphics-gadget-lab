#pragma once
#include "Lab/LabParameter.h"
#include "Parameters/HostViewParameters.h"
#include "Parameters/ParameterRequest.h"
#include "Parameters/ParameterEffectReceipt.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace gglab
{
	enum class ParameterBatchOwnerKind : uint8_t
	{
		External,
		Sequence,
		Tooling,
	};

	class ParameterBatchOwner final
	{
	public:
		constexpr ParameterBatchOwner() noexcept = default;
		[[nodiscard]] static constexpr ParameterBatchOwner Sequence(uint64_t identity) noexcept
		{
			return { ParameterBatchOwnerKind::Sequence, identity };
		}
		[[nodiscard]] static constexpr ParameterBatchOwner Tooling(uint64_t identity) noexcept
		{
			return { ParameterBatchOwnerKind::Tooling, identity };
		}
		[[nodiscard]] bool IsExternal() const noexcept { return m_Kind == ParameterBatchOwnerKind::External; }
		[[nodiscard]] ParameterBatchOwnerKind GetKind() const noexcept { return m_Kind; }
		[[nodiscard]] uint64_t GetIdentity() const noexcept { return m_Identity; }
		bool operator==(const ParameterBatchOwner&) const noexcept = default;

	private:
		constexpr ParameterBatchOwner(ParameterBatchOwnerKind kind, uint64_t identity) noexcept :
			m_Kind(kind), m_Identity(identity) {}
		ParameterBatchOwnerKind m_Kind = ParameterBatchOwnerKind::External;
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
		// A Host view operation records each item's resolved target when it applies.
		std::vector<LabParameterValue> m_Values;
		std::vector<ParameterEffectSnapshot> m_Items;
		std::string m_Failure;
		std::optional<HostViewOverrideOperation> m_HostViewOperation;
		std::optional<ParameterControlOperation> m_DomainOperation;

		[[nodiscard]] bool IsTerminal() const noexcept
		{
			return m_State == ParameterBatchState::Finished ||
				m_State == ParameterBatchState::Failed;
		}
	};
}
