#pragma once
#include "Capture/FrameCaptureMetadata.h"
#include "Lab/LabParameter.h"
#include "Parameters/HostViewParameters.h"
#include "GGLabRuntime/Graphics/Pipeline/TemporalFramePlan.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace gglab
{
	inline constexpr size_t RequiredFrameStateMaxValues = 128;

	// A display-view value the issued frame must carry, compared after Host, sequence
	// and reference patches. A source compares resolver attribution, never equality.
	struct RequiredFrameValue
	{
		LabParameterId m_Id;
		LabParameterType m_Type = LabParameterType::Bool;
		LabValue m_Value = false;
		std::optional<HostViewValueSource> m_Source;
	};

	struct RequiredTemporalConsumer
	{
		TemporalConsumer m_Consumer = TemporalConsumer::TemporalAA;
		TemporalConsumerStatus m_Status = TemporalConsumerStatus::Disabled;
		std::optional<TemporalConsumerDisableReason> m_DisableReason;
	};

	// A typed predicate a capture plan owns from admission. It is decided on the frame
	// where the plan would otherwise be issued, before any capture tap binds.
	struct RequiredFrameState
	{
		std::string m_RunIdentity;
		uint64_t m_ContentIdentity = 0;
		// A lower frame revision keeps the plan waiting; equality is not required.
		uint64_t m_MinHostViewRevision = 0;
		std::vector<RequiredFrameValue> m_Values;
		std::vector<RequiredTemporalConsumer> m_TemporalConsumers;
		std::optional<bool> m_ReferenceSampling;
	};

	enum class RequiredFrameStateVerdict : uint8_t
	{
		Match,
		// The run and content match, but the frame's Host view revision is below the minimum.
		Wait,
		Mismatch,
	};

	struct RequiredFrameStateEvaluation
	{
		RequiredFrameStateVerdict m_Verdict = RequiredFrameStateVerdict::Match;
		// Every differing field of a mismatch.
		std::string m_Differences;
	};

	// Host view IDs with their declared types, strictly valid values and distinct entries.
	// Consumer states must agree with their reasons and with how the frame plan requests
	// each consumer from the required values. Live run, content and revision are separate.
	[[nodiscard]] bool ValidateRequiredFrameState(const RequiredFrameState& state, std::string& error);
	[[nodiscard]] RequiredFrameStateEvaluation EvaluateRequiredFrameState(const RequiredFrameState& required,
		const HostViewControlStamp& control, std::span<const HostViewProjectionEntry> projection,
		const FrameCaptureTemporalState& temporal);

	// Wire names as frame state records them; unknown names have no value.
	[[nodiscard]] std::optional<TemporalConsumer> FindTemporalConsumer(std::string_view name) noexcept;
	[[nodiscard]] std::optional<TemporalConsumerStatus> FindTemporalConsumerStatus(
		std::string_view name) noexcept;
	[[nodiscard]] std::optional<TemporalConsumerDisableReason> FindTemporalConsumerDisableReason(
		std::string_view name) noexcept;
}
