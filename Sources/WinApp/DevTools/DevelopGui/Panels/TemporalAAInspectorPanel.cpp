#include "DevTools/DevelopGui/Panels/TemporalAAInspectorPanel.h"

#include "DevTools/DevelopGui/DevelopGuiContext.h"
#include "DevTools/DevelopGui/DevelopGuiTextureUtils.h"
#include "DevTools/DevToolsRuntime.h"
#include "Diagnostics/DiagnosticsRuntime.h"
#include "Diagnostics/Snapshots/TemporalAADiagnosticsSnapshot.h"
#include "Graphics/Profiling/GpuProfiler.h"
#include "Graphics/Renderer.h"
#include "Graphics/Resource/RenderResourceRegistry.h"
#include "Graphics/RHI/RHIFormat.h"

#include <imgui.h>

#include <algorithm>
#include <ranges>

namespace gglab
{
	namespace
	{
		const char* StatusName(TemporalAAFrameStatus status) noexcept
		{
			switch (status)
			{
			case TemporalAAFrameStatus::Disabled: return "Disabled";
			case TemporalAAFrameStatus::Unavailable: return "Unavailable";
			case TemporalAAFrameStatus::Active: return "Active";
			}
			return "Unknown";
		}

		const char* DisableReasonName(TemporalAADisableReason reason) noexcept
		{
			switch (reason)
			{
			case TemporalAADisableReason::None: return "None";
			case TemporalAADisableReason::NotRequested: return "Not requested";
			case TemporalAADisableReason::CoreCapabilityUnavailable:
				return "Core capability unavailable";
			case TemporalAADisableReason::DisplayViewIneligible: return "Display view ineligible";
			case TemporalAADisableReason::DepthVelocityPathUnavailable:
				return "Depth/velocity path unavailable";
			case TemporalAADisableReason::SceneExtensionUnsupported:
				return "Scene extension unsupported";
			}
			return "Unknown";
		}

		const char* ResetReasonName(TemporalHistoryResetReason reason) noexcept
		{
			switch (reason)
			{
			case TemporalHistoryResetReason::None: return "None";
			case TemporalHistoryResetReason::ColdStart: return "Cold start";
			case TemporalHistoryResetReason::Disabled: return "Disabled";
			case TemporalHistoryResetReason::DisplayViewChanged: return "Display view changed";
			case TemporalHistoryResetReason::ResetIdentityChanged: return "Reset identity changed";
			case TemporalHistoryResetReason::SessionIdentityChanged: return "Session changed";
			case TemporalHistoryResetReason::ExtentChanged: return "Extent changed";
			case TemporalHistoryResetReason::FormatChanged: return "Format changed";
			case TemporalHistoryResetReason::AllocationFailure: return "Allocation failure";
			case TemporalHistoryResetReason::AvailabilityChanged: return "Availability changed";
			case TemporalHistoryResetReason::ResolveProgramChanged:
				return "Resolve program changed";
			case TemporalHistoryResetReason::FatalSubmission: return "Fatal submission";
			case TemporalHistoryResetReason::Resume: return "Resume";
			case TemporalHistoryResetReason::Shutdown: return "Shutdown";
			}
			return "Unknown";
		}

		const char* TapName(PostProcessDebugTap tap) noexcept
		{
			switch (tap)
			{
			case PostProcessDebugTap::TemporalHistoryColor: return "Accumulated History Color";
			case PostProcessDebugTap::TemporalReprojectionUV: return "Reprojection UV";
			case PostProcessDebugTap::TemporalRejection: return "Rejection Reason";
			case PostProcessDebugTap::TemporalHistoryWeight: return "History Weight";
			case PostProcessDebugTap::TemporalHistoryAge: return "History Age";
			case PostProcessDebugTap::TemporalMotionDirection: return "Motion Direction";
			case PostProcessDebugTap::TemporalMotionMagnitude: return "Motion Magnitude";
			default: return "Temporal Preview";
			}
		}

		void DrawSettings(TemporalAASettings& settings) noexcept
		{
			ImGui::Checkbox("Enabled##TemporalAA", &settings.m_Enabled);
			ImGui::SliderFloat("Max History Feedback (Provisional)",
				&settings.m_MaxHistoryFeedback, 0.0f,
				TemporalAAProvisionalMaxHistoryFeedbackCeiling, "%.3f");
			ImGui::DragFloat("Depth Absolute Threshold", &settings.m_DepthAbsoluteThreshold,
				0.001f, 0.0f, TemporalAAMaxDepthThreshold, "%.4f");
			ImGui::DragFloat("Depth Relative Threshold", &settings.m_DepthRelativeThreshold,
				0.001f, 0.0f, TemporalAAMaxDepthThreshold, "%.4f");
			ImGui::DragFloat("Velocity Weight Scale", &settings.m_VelocityWeightScale,
				0.005f, 0.0f, TemporalAAMaxVelocityWeightScale, "%.3f");
			ImGui::DragFloat("Luminance Weight Scale", &settings.m_LuminanceWeightScale,
				0.05f, 0.0f, TemporalAAMaxLuminanceWeightScale, "%.2f");
			ImGui::DragFloat("Clamp Expansion", &settings.m_NeighborhoodClampExpansion,
				0.005f, 0.0f, TemporalAAMaxNeighborhoodClampExpansion, "%.3f");
		}
	}

	void TemporalAAInspectorPanel::Draw(DevelopGuiContext& context) noexcept
	{
		if (!context.m_Renderer || !context.m_Diagnostics)
		{
			ImGui::TextDisabled("Renderer diagnostics are unavailable.");
			return;
		}
		const auto* snapshot =
			context.m_Diagnostics->GetSnapshot<TemporalAADiagnosticsSnapshot>();
		if (!snapshot || !snapshot->m_Available)
		{
			ImGui::TextDisabled("Temporal AA diagnostics are unavailable for this frame.");
			return;
		}

		const auto& plan = snapshot->m_FramePlan;
		ImGui::Text("Status: %s | Reason: %s", StatusName(plan.m_Status),
			DisableReasonName(plan.m_DisableReason));
		ImGui::Text("Requested / Core / Active: %s / %s / %s",
			plan.m_Requested ? "yes" : "no", plan.m_CoreAvailable ? "yes" : "no",
			plan.m_Active ? "yes" : "no");

		if (ImGui::CollapsingHeader("Controls", ImGuiTreeNodeFlags_DefaultOpen))
		{
			auto* overrides = context.m_ViewRenderSettingsOverrides;
			if (!overrides)
			{
				ImGui::TextDisabled("View-render settings controls are unavailable.");
			}
			else
			{
				bool active = overrides->m_TemporalAA.m_IsActive;
				if (ImGui::Checkbox("Override Active Profile##TemporalAA", &active))
				{
					if (active && !overrides->m_TemporalAA.m_IsActive)
					{
						overrides->m_TemporalAA.m_Settings = snapshot->m_AuthoringSettings;
					}
					overrides->m_TemporalAA.m_IsActive = active;
				}
				if (active)
				{
					DrawSettings(overrides->m_TemporalAA.m_Settings);
					if (ImGui::Button("Reset Override to Active Profile"))
					{
						overrides->m_TemporalAA.m_Settings = snapshot->m_AuthoringSettings;
					}
				}
				else
				{
					TemporalAASettings readOnly = snapshot->m_RequestedSettings;
					ImGui::BeginDisabled();
					DrawSettings(readOnly);
					ImGui::EndDisabled();
				}
			}
		}

		if (ImGui::CollapsingHeader("Temporal State", ImGuiTreeNodeFlags_DefaultOpen))
		{
			const auto& history = snapshot->m_History;
			ImGui::Text("Jitter pixels: current (%.4f, %.4f)",
				snapshot->m_CurrentJitterPixels.m_X, snapshot->m_CurrentJitterPixels.m_Y);
			ImGui::Text("Jitter UV: current (%.7f, %.7f), previous (%.7f, %.7f)",
				snapshot->m_CurrentJitterUV.m_X, snapshot->m_CurrentJitterUV.m_Y,
				snapshot->m_PreviousJitterUV.m_X, snapshot->m_PreviousJitterUV.m_Y);
			ImGui::Text("History: %s | read %u | generation %llu",
				history.m_HistoryValid ? "valid" : "invalid", history.m_ReadIndex,
				static_cast<unsigned long long>(history.m_AllocationGeneration));
			ImGui::Text("Reset: %s (%llu)", ResetReasonName(history.m_LastResetReason),
				static_cast<unsigned long long>(history.m_ResetCount));
			ImGui::Text("Extent: %u x %u | color %s | depth %s", snapshot->m_Width,
				snapshot->m_Height, GetRHIFormatInfo(history.m_Compatibility.m_ColorFormat).m_Name,
				GetRHIFormatInfo(history.m_Compatibility.m_DepthFormat).m_Name);
			ImGui::Text("Logical bytes: %.2f MiB active, %.2f MiB pending retirement",
				static_cast<double>(history.m_ActiveBytes) / (1024.0 * 1024.0),
				static_cast<double>(history.m_PendingRetirementBytes) / (1024.0 * 1024.0));
			ImGui::Text("Last committed fence: %llu",
				static_cast<unsigned long long>(history.m_LastCommitted.m_GraphicsFence.m_Value));
			if (!history.m_PendingRetirementFences.empty())
			{
				ImGui::Text("Latest pending fence: %llu",
					static_cast<unsigned long long>(
						history.m_PendingRetirementFences.back().m_Value));
			}
			ImGui::Text("Resources motion / history / diagnostics: %s / %s / %s",
				snapshot->m_MotionAvailable ? "ready" : "off",
				snapshot->m_TemporalResourcesAvailable ? "ready" : "off",
				snapshot->m_ReprojectionDiagnosticsAvailable ? "ready" : "off");
		}

		if (ImGui::CollapsingHeader("Preview", ImGuiTreeNodeFlags_DefaultOpen))
		{
			constexpr PostProcessDebugTap Taps[] = {
				PostProcessDebugTap::TemporalHistoryColor,
				PostProcessDebugTap::TemporalReprojectionUV,
				PostProcessDebugTap::TemporalRejection,
				PostProcessDebugTap::TemporalHistoryWeight,
				PostProcessDebugTap::TemporalHistoryAge,
				PostProcessDebugTap::TemporalMotionDirection,
				PostProcessDebugTap::TemporalMotionMagnitude,
			};
			auto* registry = context.m_Renderer->GetRenderResourceRegistry();
			PostProcessDebugSelection selection = registry->GetPostProcessPreviewSelection();
			if (std::ranges::find(Taps, selection.m_Tap) == std::ranges::end(Taps))
			{
				selection.m_Tap = PostProcessDebugTap::TemporalHistoryWeight;
				registry->SetPostProcessPreviewSelection(selection);
			}
			if (ImGui::BeginCombo("Tap##TemporalAA", TapName(selection.m_Tap)))
			{
				for (const auto tap : Taps)
				{
					if (ImGui::Selectable(TapName(tap), tap == selection.m_Tap))
					{
						selection.m_Tap = tap;
						registry->SetPostProcessPreviewSelection(selection);
					}
				}
				ImGui::EndCombo();
			}
			registry->RequestPostProcessPreview();
			using TextureIndex = RenderResourceRegistry::TextureIndex;
			const auto* desc = registry->GetTextureDesc(TextureIndex::Preview_PostProcess);
			if (desc && registry->HasPublishedPostProcessPreview() &&
				registry->GetPublishedPostProcessPreviewSelection() == selection)
			{
				const ImTextureID textureId = devtools::ResolveImGuiTextureId(
					context.m_DevelopGuiSystem,
					registry->GetSrvDescriptor(TextureIndex::Preview_PostProcess));
				if (textureId)
				{
					const float width = std::clamp(ImGui::GetContentRegionAvail().x, 64.0f, 768.0f);
					const float aspect = static_cast<float>(desc->m_Extent.m_Height) /
						static_cast<float>(desc->m_Extent.m_Width);
					ImGui::Image(textureId, ImVec2(width, width * aspect));
				}
			}
			else
			{
				ImGui::TextDisabled(plan.m_Active ? "Preview update pending..."
					: "The selected preview requires an active TAA frame.");
			}
		}

		if (ImGui::CollapsingHeader("GPU Timing", ImGuiTreeNodeFlags_DefaultOpen))
		{
			GpuProfiler* profiler = context.m_Renderer->GetGpuProfiler();
			if (profiler)
			{
				bool enabled = profiler->IsEnabled();
				if (ImGui::Checkbox("GPU Profiling##TemporalAA", &enabled))
				{
					profiler->SetEnabled(enabled);
				}
			}
			if (snapshot->m_GpuTimingAvailable)
			{
				ImGui::Text("Resolve %.3f ms | frame %llu", snapshot->m_ResolveGpuMilliseconds,
					static_cast<unsigned long long>(snapshot->m_GpuFrameIndex));
			}
			else
			{
				ImGui::TextDisabled("Waiting for a completed GPU timestamp frame...");
			}
		}
	}
}
