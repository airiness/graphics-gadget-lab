#include "Capture/RequiredFrameState.h"
#include "GGLabRuntime/Graphics/ViewRenderSettings.h"

#include <algorithm>
#include <array>
#include <format>
#include <type_traits>
#include <variant>

namespace gglab
{
	namespace
	{
		constexpr std::array TemporalConsumerStatuses{ TemporalConsumerStatus::Disabled,
			TemporalConsumerStatus::Unavailable, TemporalConsumerStatus::Active };
		constexpr std::array TemporalConsumerDisableReasons{ TemporalConsumerDisableReason::None,
			TemporalConsumerDisableReason::NotRequested, TemporalConsumerDisableReason::CoreCapabilityUnavailable,
			TemporalConsumerDisableReason::DisplayViewIneligible,
			TemporalConsumerDisableReason::DepthVelocityPathUnavailable,
			TemporalConsumerDisableReason::SceneExtensionUnsupported };

		[[nodiscard]] std::string FormatValue(const LabValue& value)
		{
			return std::visit([](const auto& item) -> std::string
				{
					using T = std::decay_t<decltype(item)>;
					if constexpr (std::is_same_v<T, Vector3>)
					{
						return std::format("[{}, {}, {}]", item.m_X, item.m_Y, item.m_Z);
					}
					else if constexpr (std::is_same_v<T, Color>)
					{
						return std::format("[{}, {}, {}, {}]", item.m_R, item.m_G, item.m_B, item.m_A);
					}
					else
					{
						return std::format("{}", item);
					}
				}, value);
		}

		void AddDifference(std::string& differences, std::string difference)
		{
			differences += differences.empty() ? "" : "; ";
			differences += difference;
		}

		// The display-view values the frame plan resolves the accumulator requests from.
		constexpr std::string_view TemporalAAEnabledId = "view.taa.enabled";
		constexpr std::string_view GTAOEnabledId = "view.gtao.enabled";
		constexpr std::string_view GTAOTemporalAccumulationId = "view.gtao.temporal_accumulation";

		[[nodiscard]] std::optional<bool> GetRequiredBool(const RequiredFrameState& state, std::string_view id)
		{
			const auto found = std::ranges::find_if(state.m_Values,
				[&](const RequiredFrameValue& value) { return value.m_Id.GetName() == id; });
			const bool* value = found != state.m_Values.end() ? std::get_if<bool>(&found->m_Value) : nullptr;
			return value ? std::optional(*value) : std::nullopt;
		}

		// Whether the required values decide that the plan requests a consumer.
		[[nodiscard]] std::optional<bool> GetRequiredConsumerRequest(
			const RequiredFrameState& state, TemporalConsumer consumer)
		{
			if (consumer == TemporalConsumer::Reference)
			{
				return state.m_ReferenceSampling;
			}
			// A reference sample holds both accumulators unrequested.
			if (state.m_ReferenceSampling == true)
			{
				return false;
			}
			if (consumer == TemporalConsumer::TemporalAA)
			{
				return GetRequiredBool(state, TemporalAAEnabledId);
			}
			const std::optional<bool> enabled = GetRequiredBool(state, GTAOEnabledId);
			const std::optional<bool> accumulation = GetRequiredBool(state, GTAOTemporalAccumulationId);
			if (enabled == false || accumulation == false)
			{
				return false;
			}
			return enabled.has_value() && accumulation.has_value() ? std::optional(true) : std::nullopt;
		}

		// Disabled carries NotRequested, Active None and Unavailable a capability reason. The
		// reference has no eligibility gate, so it is never unavailable.
		[[nodiscard]] bool IsConsistentConsumerState(const RequiredTemporalConsumer& consumer) noexcept
		{
			if (consumer.m_Consumer == TemporalConsumer::Reference &&
				consumer.m_Status == TemporalConsumerStatus::Unavailable)
			{
				return false;
			}
			if (!consumer.m_DisableReason)
			{
				return true;
			}
			const TemporalConsumerDisableReason reason = *consumer.m_DisableReason;
			switch (consumer.m_Status)
			{
			case TemporalConsumerStatus::Disabled:
				return reason == TemporalConsumerDisableReason::NotRequested;
			case TemporalConsumerStatus::Active:
				return reason == TemporalConsumerDisableReason::None;
			case TemporalConsumerStatus::Unavailable:
				return reason != TemporalConsumerDisableReason::None &&
					reason != TemporalConsumerDisableReason::NotRequested;
			}
			return false;
		}
	}

	bool ValidateRequiredFrameState(const RequiredFrameState& state, std::string& error)
	{
		error.clear();
		if (state.m_Values.empty() || state.m_Values.size() > RequiredFrameStateMaxValues)
		{
			error = "A required frame state takes 1-128 Host view values.";
			return false;
		}
		const LabParameterSet schema = DescribeHostViewParameters(ResolvedViewRenderSettings{});
		for (size_t index = 0; index < state.m_Values.size(); ++index)
		{
			const RequiredFrameValue& value = state.m_Values[index];
			const LabParameter* parameter = schema.Find(value.m_Id);
			if (!parameter || parameter->m_Desc.m_Type != value.m_Type)
			{
				error = std::format("'{}' is not a Host view parameter of the requested type.",
					value.m_Id.GetName());
				return false;
			}
			if (std::ranges::any_of(std::span(state.m_Values).first(index),
				[&](const RequiredFrameValue& previous) { return previous.m_Id == value.m_Id; }))
			{
				error = "Required frame values must name distinct Host view parameters.";
				return false;
			}
			if (!LabParameterSet::ValidateValue(parameter->m_Desc, value.m_Value, error))
			{
				error = std::format("Required '{}': {}.", value.m_Id.GetName(), error);
				return false;
			}
		}
		for (size_t index = 0; index < state.m_TemporalConsumers.size(); ++index)
		{
			const RequiredTemporalConsumer& consumer = state.m_TemporalConsumers[index];
			if (static_cast<uint32_t>(consumer.m_Consumer) >= TemporalConsumerCount ||
				std::ranges::any_of(std::span(state.m_TemporalConsumers).first(index),
					[&](const RequiredTemporalConsumer& previous)
					{
						return previous.m_Consumer == consumer.m_Consumer;
					}))
			{
				error = "Required temporal consumers must name distinct known consumers.";
				return false;
			}
			if (!IsConsistentConsumerState(consumer))
			{
				error = std::format("Required '{}' combines a status with a contradictory disable reason.",
					GetTemporalConsumerName(consumer.m_Consumer));
				return false;
			}
			// The frame plan resolves Disabled exactly when the consumer is unrequested.
			const std::optional<bool> requested = GetRequiredConsumerRequest(state, consumer.m_Consumer);
			if (requested && *requested == (consumer.m_Status == TemporalConsumerStatus::Disabled))
			{
				error = std::format("Required '{}' {} contradicts the required values that request it.",
					GetTemporalConsumerName(consumer.m_Consumer),
					GetTemporalConsumerStatusName(consumer.m_Status));
				return false;
			}
		}
		// A reference sample forces both accumulators unrequested and their masked values false.
		if (state.m_ReferenceSampling == true &&
			(GetRequiredBool(state, TemporalAAEnabledId) == true ||
				GetRequiredBool(state, GTAOTemporalAccumulationId) == true))
		{
			error = "Required reference sampling contradicts a required temporal accumulation value.";
			return false;
		}
		return true;
	}

	RequiredFrameStateEvaluation EvaluateRequiredFrameState(const RequiredFrameState& required,
		const HostViewControlStamp& control, std::span<const HostViewProjectionEntry> projection,
		const FrameCaptureTemporalState& temporal)
	{
		RequiredFrameStateEvaluation evaluation;
		std::string& differences = evaluation.m_Differences;
		if (control.m_RunIdentity != required.m_RunIdentity)
		{
			AddDifference(differences, "the frame belongs to another run");
		}
		if (control.m_ContentIdentity != required.m_ContentIdentity)
		{
			AddDifference(differences, std::format("content identity {}, required {}",
				control.m_ContentIdentity, required.m_ContentIdentity));
		}
		if (!differences.empty())
		{
			evaluation.m_Verdict = RequiredFrameStateVerdict::Mismatch;
			return evaluation;
		}
		if (control.m_HostViewRevision < required.m_MinHostViewRevision)
		{
			evaluation.m_Verdict = RequiredFrameStateVerdict::Wait;
			return evaluation;
		}
		for (const RequiredFrameValue& value : required.m_Values)
		{
			const auto entry = std::ranges::find(projection, value.m_Id, &HostViewProjectionEntry::m_Id);
			if (entry == projection.end())
			{
				AddDifference(differences, std::format("{} is not projected", value.m_Id.GetName()));
				continue;
			}
			if (!LabValuesEqual(entry->m_Value, value.m_Value))
			{
				AddDifference(differences, std::format("{} value {}, required {}", value.m_Id.GetName(),
					FormatValue(entry->m_Value), FormatValue(value.m_Value)));
			}
			if (value.m_Source && entry->m_Source != *value.m_Source)
			{
				AddDifference(differences, std::format("{} source {}, required {}", value.m_Id.GetName(),
					GetHostViewValueSourceName(entry->m_Source), GetHostViewValueSourceName(*value.m_Source)));
			}
		}
		const auto findConsumer = [&](TemporalConsumer consumer)
			{
				const auto found = std::ranges::find(temporal.m_Consumers, GetTemporalConsumerName(consumer),
					&FrameCaptureTemporalConsumer::m_Name);
				return found != temporal.m_Consumers.end() ? &*found : nullptr;
			};
		for (const RequiredTemporalConsumer& consumer : required.m_TemporalConsumers)
		{
			const std::string_view name = GetTemporalConsumerName(consumer.m_Consumer);
			const FrameCaptureTemporalConsumer* observed = findConsumer(consumer.m_Consumer);
			if (!observed)
			{
				AddDifference(differences, std::format("{} is not planned", name));
				continue;
			}
			if (observed->m_Status != GetTemporalConsumerStatusName(consumer.m_Status))
			{
				AddDifference(differences, std::format("{} status {}, required {}", name,
					observed->m_Status, GetTemporalConsumerStatusName(consumer.m_Status)));
			}
			if (consumer.m_DisableReason &&
				observed->m_DisableReason != GetTemporalConsumerDisableReasonName(*consumer.m_DisableReason))
			{
				AddDifference(differences, std::format("{} disable reason {}, required {}", name,
					observed->m_DisableReason, GetTemporalConsumerDisableReasonName(*consumer.m_DisableReason)));
			}
		}
		if (required.m_ReferenceSampling)
		{
			const FrameCaptureTemporalConsumer* reference = findConsumer(TemporalConsumer::Reference);
			const bool sampling = reference && reference->m_Requested;
			if (sampling != *required.m_ReferenceSampling)
			{
				AddDifference(differences, std::format("reference sampling {}, required {}",
					sampling, *required.m_ReferenceSampling));
			}
		}
		evaluation.m_Verdict = differences.empty() ?
			RequiredFrameStateVerdict::Match :
			RequiredFrameStateVerdict::Mismatch;
		return evaluation;
	}

	std::optional<TemporalConsumer> FindTemporalConsumer(std::string_view name) noexcept
	{
		for (uint32_t index = 0; index < TemporalConsumerCount; ++index)
		{
			if (GetTemporalConsumerName(static_cast<TemporalConsumer>(index)) == name)
			{
				return static_cast<TemporalConsumer>(index);
			}
		}
		return std::nullopt;
	}

	std::optional<TemporalConsumerStatus> FindTemporalConsumerStatus(std::string_view name) noexcept
	{
		const auto found = std::ranges::find(TemporalConsumerStatuses, name, &GetTemporalConsumerStatusName);
		return found != TemporalConsumerStatuses.end() ? std::optional(*found) : std::nullopt;
	}

	std::optional<TemporalConsumerDisableReason> FindTemporalConsumerDisableReason(std::string_view name) noexcept
	{
		const auto found = std::ranges::find(TemporalConsumerDisableReasons, name,
			&GetTemporalConsumerDisableReasonName);
		return found != TemporalConsumerDisableReasons.end() ? std::optional(*found) : std::nullopt;
	}
}
