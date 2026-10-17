#include "Application/SelfTest/TemporalAALabDiagnosticsSelfTests.h"

#include "Application/Lab/Sessions/TemporalAALabDiagnostics.h"
#include "GGLabRuntime/Diagnostics/Snapshots/LabSnapshot.h"
#include "GGLabRuntime/Graphics/Pipeline/TemporalAA.h"
#include "GGLabRuntime/Graphics/Pipeline/TemporalFramePlan.h"
#include "GGLabRuntime/Graphics/Pipeline/TemporalHistoryTypes.h"
#include "GGLabTestCore/SelfTest.h"

#include <cstdint>
#include <string>
#include <string_view>

namespace gglab
{
	void RunTemporalAALabDiagnosticsSelfTests(SelfTestContext& context) noexcept
	{
		TemporalAASettings authoring{};
		authoring.m_Enabled = false;
		TemporalAASettings requested = authoring;
		requested.m_Enabled = true;
		requested.m_HistoryFilter = TemporalAAHistoryFilter::Bilinear;
		requested.m_MaxHistoryFeedback = 0.5f;
		requested.m_ResolutionPreset = TemporalAAResolutionPreset::Quality;
		ResolvedTemporalFramePlan plan{};
		auto& consumer = plan.m_Consumers[static_cast<uint32_t>(TemporalConsumer::TemporalAA)];
		consumer = { .m_Status = TemporalConsumerStatus::Active,
			.m_DisableReason = TemporalConsumerDisableReason::None, .m_Requested = true };
		plan.m_TemporalAA.m_ResolutionPreset = TemporalAAResolutionPreset::Quality;
		TemporalHistoryManagerDiagnostics history{};
		history.m_HasActiveHistory = true;
		LabDiagnosticsFrameContext frame{ .m_RequestedTemporalAASettings = &requested,
			.m_TemporalFramePlan = &plan, .m_FrameSerial = 82 };
		const auto capture = [&]()
		{
			LabDiagnosticsSnapshot snapshot;
			AppendTemporalAALabSettingsDiagnostics(snapshot, authoring, frame, history);
			return snapshot;
		};
		const auto metric = [](const LabDiagnosticsSnapshot& snapshot, std::string_view name)
		{
			for (const auto& entry : snapshot.m_Metrics)
			{
				if (entry.m_Name == name)
				{
					return entry.m_Value;
				}
			}
			return std::string{};
		};
		const auto active = capture();
		context.Check(metric(active, "TAA authoring settings").find("disabled;") == 0 &&
			metric(active, "TAA authoring settings").find("history catmull-rom-clamped") != std::string::npos &&
			metric(active, "TAA requested settings").find("enabled;") == 0 &&
			metric(active, "TAA requested settings").find("history bilinear") != std::string::npos,
			"Lab diagnostics distinguish disabled authoring from an active overridden history filter");
		context.Check(metric(active, "Requested accumulation bound").find("samples 1.00") != std::string::npos,
			"Lab accumulation diagnostics derive capacity from requested feedback");
		context.Check(active.m_Checks.front().m_Status == LabDiagnosticCheckStatus::Passed &&
			metric(active, "TAA runtime state").find("frame 82; active (none); effective resolution quality") == 0,
			"Lab runtime diagnostics identify the current active frame and allocated history");
		plan.m_TemporalAA.m_ResolutionPreset = TemporalAAResolutionPreset::Native;
		const auto nativeFallback = capture();
		context.Check(metric(nativeFallback, "TAA requested settings").find("resolution quality") != std::string::npos &&
			metric(nativeFallback, "TAA runtime state").find("effective resolution native") != std::string::npos,
			"Requested Quality remains distinct from a frame plan constrained to Native");
		authoring.m_Enabled = true;
		requested.m_Enabled = false;
		consumer = {};
		context.Check(capture().m_Checks.front().m_Status == LabDiagnosticCheckStatus::Pending,
			"Disabling TAA cannot pass the allocation check using retained history");
		consumer.m_Status = TemporalConsumerStatus::Unavailable;
		consumer.m_DisableReason = TemporalConsumerDisableReason::DepthVelocityPathUnavailable;
		requested.m_Enabled = true;
		const auto unavailable = capture();
		context.Check(unavailable.m_Checks.front().m_Status == LabDiagnosticCheckStatus::Pending &&
			metric(unavailable, "TAA runtime state").find("depth-velocity-path-unavailable") != std::string::npos,
			"An enabled request with an unavailable consumer reports its reason and cannot pass allocation");
		consumer.m_Status = TemporalConsumerStatus::Active;
		consumer.m_DisableReason = TemporalConsumerDisableReason::None;
		history.m_HasActiveHistory = false;
		context.Check(capture().m_Checks.front().m_Status == LabDiagnosticCheckStatus::Pending,
			"An active plan still needs an actual history allocation");
		frame.m_RequestedTemporalAASettings = nullptr;
		const auto missingSettings = capture();
		frame.m_RequestedTemporalAASettings = &requested;
		frame.m_TemporalFramePlan = nullptr;
		const auto missingPlan = capture();
		context.Check(metric(missingSettings, "TAA runtime state") == "unavailable" &&
			metric(missingPlan, "Requested accumulation bound") == "unavailable" &&
			missingPlan.m_Checks.front().m_Status == LabDiagnosticCheckStatus::Pending &&
			metric(active, "TAA runtime state").find("effective resolution quality") != std::string::npos,
			"Incomplete frames remain unknown and cannot alter previously published Lab diagnostics");
	}
}
