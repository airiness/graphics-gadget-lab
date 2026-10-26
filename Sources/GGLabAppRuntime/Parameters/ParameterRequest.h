#pragma once
#include "Lab/LabParameter.h"
#include "Parameters/HostViewParameters.h"

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace gglab
{
	enum class ParameterOperation : uint8_t
	{
		Assign,
		Pin,
		RemoveOverride,
	};

	struct ParameterRequestEntry
	{
		LabParameterId m_Id;
		LabParameterType m_Type = LabParameterType::Bool;
		ParameterOperation m_Operation = ParameterOperation::Assign;
		std::optional<LabValue> m_Value;
	};

	// Caller-owned intent. Admission copies it into an immutable batch snapshot;
	// resolved targets and control/effect observations live in the result instead.
	struct ParameterRequest
	{
		std::vector<ParameterRequestEntry> m_Entries;
		// Assign batches are unguarded; Host Pin/removal batches require this stamp.
		// AppRuntime verifies the run before entering the Session transaction owner.
		std::optional<HostViewControlStamp> m_Expected;

		[[nodiscard]] static ParameterRequest HostViewOverrides(const HostViewControlStamp& expected,
			std::span<const HostViewOverrideState> overrides)
		{
			ParameterRequest request{ .m_Expected = expected };
			request.m_Entries.reserve(overrides.size());
			for (const auto& state : overrides)
			{
				request.m_Entries.push_back({ state.m_Id, state.m_Type,
					state.m_Value ? ParameterOperation::Pin : ParameterOperation::RemoveOverride, state.m_Value });
			}
			return request;
		}
	};
}
