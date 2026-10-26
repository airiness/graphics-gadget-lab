#pragma once
#include "Lab/LabParameter.h"
#include "Parameters/HostViewParameters.h"

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace gglab
{
	enum class ParameterControlDomain : uint8_t
	{
		Lab,
		LightingProfile,
	};

	struct ParameterControlStamp
	{
		std::string m_RunIdentity;
		uint64_t m_ContentIdentity = 0;
		uint64_t m_ActivationRevision = 0;
		ParameterControlDomain m_Domain = ParameterControlDomain::Lab;
		uint64_t m_LabRevision = 0;
		uint64_t m_ValuesRevision = 0;
		uint64_t m_WorldRevision = 0;
		uint64_t m_EnvironmentRevision = 0;
		bool operator==(const ParameterControlStamp&) const noexcept = default;
	};

	struct ParameterControlOperation
	{
		ParameterControlStamp m_Expected;
		std::optional<ParameterControlStamp> m_Before;
		std::optional<ParameterControlStamp> m_After;
		bool m_Committed = false;
		std::string m_Conflict;
	};

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
		// Host Pin/removal requires this stamp; legacy Assign leaves it absent.
		// AppRuntime verifies the run before entering the Session transaction owner.
		std::optional<HostViewControlStamp> m_Expected;
		// Protected Assign in exactly one Lab/profile domain; never combined with
		// Host Pin/removal. Effect generations are not concurrency tokens.
		std::optional<ParameterControlStamp> m_ExpectedDomain;

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
