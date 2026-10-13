#include "Application/Lab/Sessions/TemporalAALabDiagnostics.h"

#include "GGLabRuntime/Diagnostics/Snapshots/LabSnapshot.h"
#include "GGLabRuntime/Graphics/Pipeline/TemporalAA.h"
#include "GGLabRuntime/Graphics/Pipeline/TemporalFramePlan.h"
#include "GGLabRuntime/Graphics/Pipeline/TemporalHistoryTypes.h"

#include <format>
#include <string>
#include <utility>

namespace gglab
{
	std::string FormatTemporalAALabSettings(const TemporalAASettings& settings) noexcept
	{
		return std::format(
			"{}; feedback {:.6f}, velocity {:.6f}, luminance {:.6f}, clamp {:.6f}, "
			"relaxation {:.2f}, accumulation {}, rectification {} (gamma {:.2f}), "
			"history {}, current {}, motion {}, post-temporal {}, resolution {}, "
			"texture LOD {:.2f}, depth absolute {:.6f}, relative {:.6f}",
			settings.m_Enabled ? "enabled" : "disabled",
			settings.m_MaxHistoryFeedback, settings.m_VelocityWeightScale,
			settings.m_LuminanceWeightScale, settings.m_NeighborhoodClampExpansion,
			settings.m_HistoryRelaxation,
			GetTemporalAAHistoryAccumulationName(settings.m_HistoryAccumulation),
			GetTemporalAAHistoryRectificationName(settings.m_HistoryRectification),
			settings.m_VarianceClipGamma,
			GetTemporalAAHistoryFilterName(settings.m_HistoryFilter),
			GetTemporalAACurrentFilterName(settings.m_CurrentFilter),
			GetTemporalAAMotionSelectionName(settings.m_MotionSelection),
			GetTemporalAAPostTemporalViewName(settings.m_PostTemporalView),
			GetTemporalAAResolutionPresetName(settings.m_ResolutionPreset),
			settings.m_TextureLodBiasOffset, settings.m_DepthAbsoluteThreshold,
			settings.m_DepthRelativeThreshold);
	}

	void AppendTemporalAALabSettingsDiagnostics(LabDiagnosticsSnapshot& diagnostics,
		const TemporalAASettings& authoring, const LabDiagnosticsFrameContext& context,
		const TemporalHistoryManagerDiagnostics& history) noexcept
	{
		diagnostics.m_Metrics.push_back({ .m_Name = "TAA authoring settings",
			.m_Value = FormatTemporalAALabSettings(authoring) });
		// Require both inputs from the same capture. Never infer runtime activity
		// from enabled authoring settings or a retained history allocation.
		const bool available = context.m_RequestedTemporalAASettings && context.m_TemporalFramePlan;
		LabDiagnosticCheck allocation{ .m_Name = "Temporal history allocation" };
		if (!available)
		{
			diagnostics.m_Metrics.push_back({ .m_Name = "TAA requested settings",
				.m_Value = "unavailable (no display-frame settings/plan)" });
			diagnostics.m_Metrics.push_back({ .m_Name = "TAA runtime state", .m_Value = "unavailable" });
			diagnostics.m_Metrics.push_back({ .m_Name = "Requested accumulation bound",
				.m_Value = "unavailable" });
			allocation.m_Detail = "A current display-frame plan is required to assess allocation.";
		}
		else
		{
			const auto& requested = *context.m_RequestedTemporalAASettings;
			const auto& plan = *context.m_TemporalFramePlan;
			const auto& consumer = plan.GetConsumer(TemporalConsumer::TemporalAA);
			diagnostics.m_Metrics.push_back({ .m_Name = "TAA requested settings",
				.m_Value = FormatTemporalAALabSettings(requested) });
			diagnostics.m_Metrics.push_back({ .m_Name = "TAA runtime state", .m_Value = std::format(
				"frame {}; {} ({}); effective resolution {}",
				context.m_FrameSerial, GetTemporalConsumerStatusName(consumer.m_Status),
				GetTemporalConsumerDisableReasonName(consumer.m_DisableReason),
				GetTemporalAAResolutionPresetName(plan.m_TemporalAA.m_ResolutionPreset)) });
			diagnostics.m_Metrics.push_back({ .m_Name = "Requested accumulation bound",
				.m_Value = std::format("age saturation {:.0f}, samples {:.2f}, stored max {:.0f}",
					ResolveTemporalAAFeedbackSaturationAge(requested.m_MaxHistoryFeedback),
					ResolveTemporalAAMaxHistorySamples(requested.m_MaxHistoryFeedback),
					TemporalHistoryMaxAccumulation) });
			allocation.m_Status = consumer.IsActive() && history.m_HasActiveHistory
				? LabDiagnosticCheckStatus::Passed : LabDiagnosticCheckStatus::Pending;
			allocation.m_Detail = consumer.IsActive()
				? (history.m_HasActiveHistory ? "The current TAA plan has an active history allocation."
					: "The current TAA plan is active; history allocation is pending.")
				: std::format("TAA is {} ({}); retained history alone does not establish activity.",
					GetTemporalConsumerStatusName(consumer.m_Status),
					GetTemporalConsumerDisableReasonName(consumer.m_DisableReason));
		}
		diagnostics.m_Checks.push_back(std::move(allocation));
	}
}
