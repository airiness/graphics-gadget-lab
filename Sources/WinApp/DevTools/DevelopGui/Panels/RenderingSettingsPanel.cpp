#include "DevTools/DevelopGui/Panels/RenderingSettingsPanel.h"

#include "DevTools/DevToolsRuntime.h"
#include "DevTools/DevelopGui/DevelopGuiContext.h"
#include "DevTools/DevelopGui/DevelopGuiRegistry.h"
#include "GGLabRuntime/Diagnostics/DiagnosticsView.h"
#include "GGLabRuntime/Diagnostics/Snapshots/RenderingSettingsDiagnosticsSnapshot.h"

#include <imgui.h>

#include <optional>
#include <string_view>

namespace gglab
{
	namespace
	{
		const ImVec4 ActiveColor{ 0.45f, 0.9f, 0.55f, 1.0f };
		const ImVec4 WarningColor{ 1.0f, 0.75f, 0.3f, 1.0f };
		const ImVec4 MutedColor{ 0.65f, 0.65f, 0.65f, 1.0f };
		constexpr std::string_view ForwardInspector = "Rendering/Forward+";
		constexpr std::string_view GTAOInspector = "Rendering/Ambient Occlusion";
		constexpr std::string_view TemporalInspector = "Rendering/Temporal AA";
		constexpr std::string_view PostProcessInspector = "Rendering/Post Processing";
		constexpr std::string_view ShadowInspector = "Rendering/Shadows";

		const char* ReasonText(ViewRenderFeatureReason reason) noexcept
		{
			switch (reason)
			{
			case ViewRenderFeatureReason::None: return "";
			case ViewRenderFeatureReason::NotRequested: return "Not requested";
			case ViewRenderFeatureReason::FrameUnavailable: return "Current frame unavailable";
			case ViewRenderFeatureReason::PipelineUnavailable: return "Pipeline unavailable";
			case ViewRenderFeatureReason::CoreCapabilityUnavailable: return "Required capabilities unavailable";
			case ViewRenderFeatureReason::NoOpaqueDraws: return "No opaque draws";
			case ViewRenderFeatureReason::DisplayViewIneligible: return "Display view ineligible";
			case ViewRenderFeatureReason::DepthVelocityPathUnavailable: return "Depth / velocity path unavailable";
			case ViewRenderFeatureReason::SceneExtensionUnsupported: return "Scene extension unsupported";
			case ViewRenderFeatureReason::MaterialDiagnosticsActive: return "Material diagnostics active";
			case ViewRenderFeatureReason::RequiredFeatureInactive: return "Required feature inactive";
			case ViewRenderFeatureReason::ZeroIntensity: return "Intensity is zero";
			case ViewRenderFeatureReason::ResourcesUnavailable: return "Resources unavailable";
			case ViewRenderFeatureReason::RenderGraphCulled: return "Work culled from the render graph";
			}
			GGLAB_UNREACHABLE("Unhandled rendering feature reason.");
		}

		void DrawRuntime(ViewRenderFeatureStatus status, bool pending = false,
			const char* detail = nullptr) noexcept
		{
			ImGui::TableSetColumnIndex(2);
			const char* label = "Unavailable";
			ImVec4 color = WarningColor;
			switch (status.m_State)
			{
			case ViewRenderFeatureState::Active: label = "Active"; color = ActiveColor; break;
			case ViewRenderFeatureState::Disabled: label = "Disabled"; color = MutedColor; break;
			case ViewRenderFeatureState::Inactive: label = "Inactive"; break;
			case ViewRenderFeatureState::Unavailable: break;
			}
			ImGui::TextColored(color, "%s", label);
			if (detail && status.m_State == ViewRenderFeatureState::Active)
			{
				ImGui::SameLine();
				ImGui::TextDisabled("%s", detail);
			}
			if (status.m_Reason != ViewRenderFeatureReason::None &&
				status.m_Reason != ViewRenderFeatureReason::NotRequested)
			{
				ImGui::TextWrapped("%s", ReasonText(status.m_Reason));
			}
			if (pending)
			{
				ImGui::TextColored(WarningColor, "Change pending");
			}
		}

		bool BeginSettingsTable(const char* section) noexcept
		{
			if (!ImGui::BeginTable(section, 5, ImGuiTableFlags_RowBg |
				ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_BordersInnerH))
			{
				return false;
			}
			ImGui::TableSetupColumn("Feature", ImGuiTableColumnFlags_WidthStretch, 1.2f);
			ImGui::TableSetupColumn("Requested", ImGuiTableColumnFlags_WidthFixed, 104.0f);
			ImGui::TableSetupColumn("Runtime", ImGuiTableColumnFlags_WidthStretch, 2.0f);
			ImGui::TableSetupColumn("Source", ImGuiTableColumnFlags_WidthFixed, 114.0f);
			ImGui::TableSetupColumn("Details", ImGuiTableColumnFlags_WidthFixed, 64.0f);
			ImGui::TableHeadersRow();
			return true;
		}

		void BeginRow(const char* label) noexcept
		{
			ImGui::PushID(label);
			ImGui::TableNextRow();
			ImGui::TableSetColumnIndex(0);
			ImGui::AlignTextToFramePadding();
			ImGui::TextWrapped("%s", label);
			ImGui::TableSetColumnIndex(1);
		}

		void DrawInspect(DevelopGuiContext& context, std::string_view path) noexcept
		{
			ImGui::TableSetColumnIndex(4);
			if (!path.empty())
			{
				ImGui::BeginDisabled(!context.m_PanelRegistry);
				if (ImGui::SmallButton("Inspect"))
				{
					GGLAB_UNUSED(context.m_PanelRegistry->OpenPanel(path));
				}
				ImGui::EndDisabled();
			}
			ImGui::PopID();
		}

		template <typename RESET> void DrawOverride(bool active, RESET&& reset) noexcept
		{
			ImGui::TableSetColumnIndex(3);
			if (active)
			{
				ImGui::TextColored(WarningColor, "Override");
				ImGui::SameLine();
				if (ImGui::SmallButton("Reset")) reset();
				if (ImGui::IsItemHovered()) ImGui::SetTooltip("Restore the current authoring profile.");
			}
			else
			{
				ImGui::TextDisabled("Profile");
			}
		}

		void DrawIntentTooltip(bool authoring, bool resolved) noexcept
		{
			if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
			{
				ImGui::SetTooltip("Authoring: %s\nResolved for this frame: %s",
					authoring ? "ON" : "OFF", resolved ? "ON" : "OFF");
			}
		}

		template <typename T> void DrawBlock(const char* label, SettingsOverride<T>* override,
			const T& authoring, const T& published, bool resolved, ViewRenderFeatureStatus status,
			DevelopGuiContext& context, std::string_view inspector, const char* detail = nullptr) noexcept
		{
			BeginRow(label);
			bool enabled = override ? (override->m_IsActive
				? override->m_Settings.m_Enabled : authoring.m_Enabled) : published.m_Enabled;
			ImGui::BeginDisabled(!override);
			if (ImGui::Checkbox("##Enabled", &enabled))
			{
				// Seed only the first edit; toggling never discards Inspector tuning.
				if (!override->m_IsActive) override->Activate(authoring);
				override->m_Settings.m_Enabled = enabled;
			}
			DrawIntentTooltip(authoring.m_Enabled, resolved);
			ImGui::EndDisabled();
			DrawOverride(override && override->m_IsActive, [&]() { override->Reset(); });
			const bool requested = override ? (override->m_IsActive
				? override->m_Settings.m_Enabled : authoring.m_Enabled) : published.m_Enabled;
			DrawRuntime(status, requested != published.m_Enabled, detail);
			DrawInspect(context, inspector);
		}

		void DrawScalar(const char* label, std::optional<bool>* override,
			bool authoring, bool published, bool resolved, ViewRenderFeatureStatus status,
			DevelopGuiContext& context, std::string_view inspector) noexcept
		{
			BeginRow(label);
			bool enabled = override ? override->value_or(authoring) : published;
			ImGui::BeginDisabled(!override);
			if (ImGui::Checkbox("##Enabled", &enabled)) *override = enabled;
			DrawIntentTooltip(authoring, resolved);
			ImGui::EndDisabled();
			DrawOverride(override && override->has_value(), [&]() { override->reset(); });
			DrawRuntime(status, (override ? override->value_or(authoring) : published) != published);
			DrawInspect(context, inspector);
		}

		void DrawReadOnly(const char* label, const char* requested, ViewRenderFeatureStatus status,
			DevelopGuiContext& context, std::string_view inspector) noexcept
		{
			BeginRow(label);
			ImGui::TextUnformatted(requested);
			ImGui::TableSetColumnIndex(3);
			ImGui::TextDisabled("Read only");
			DrawRuntime(status);
			DrawInspect(context, inspector);
		}
	}

	void RenderingSettingsPanel::Draw(DevelopGuiContext& context) noexcept
	{
		ImGui::SetWindowSize(ImVec2(800.0f, 680.0f), ImGuiCond_FirstUseEver);
		ImGui::SeparatorText("Profile");
		const std::string_view profileName = context.m_ActiveProfileName.empty()
			? "Unavailable" : context.m_ActiveProfileName;
		ImGui::Text("Active Profile: %.*s", static_cast<int>(profileName.size()), profileName.data());
		auto* overrides = context.m_ViewRenderSettingsOverrides;
		const uint32_t activeOverrides = overrides ? overrides->GetActiveCount() : 0;
		ImGui::TextColored(activeOverrides > 0 ? WarningColor : MutedColor,
			"%u DevTools Overrides Active", activeOverrides);
		ImGui::SameLine();
		ImGui::BeginDisabled(!overrides || activeOverrides == 0);
		if (ImGui::SmallButton("Clear All Overrides")) overrides->ClearAll();
		ImGui::EndDisabled();

		const auto* snapshot = context.m_Diagnostics
			? context.m_Diagnostics->GetSnapshot<RenderingSettingsDiagnosticsSnapshot>() : nullptr;
		if (!snapshot || !snapshot->m_SettingsAvailable)
		{
			ImGui::TextWrapped("Rendering settings are unavailable for the current frame.");
			return;
		}
		const auto& authoring = snapshot->m_AuthoringProfile;
		const auto& requested = snapshot->m_RequestedProfile;
		const auto& resolved = snapshot->m_ResolvedSettings;
		if (ImGui::CollapsingHeader("Lighting", ImGuiTreeNodeFlags_DefaultOpen) && BeginSettingsTable("LightingSettings"))
		{
			DrawReadOnly("Lighting Path", "Forward+", snapshot->m_ForwardLighting, context, ForwardInspector);
			DrawBlock("GTAO", overrides ? &overrides->m_GTAO : nullptr,
				authoring.m_Lighting.m_GTAO, requested.m_Lighting.m_GTAO, resolved.m_Lighting.m_GTAO.m_Enabled,
				snapshot->m_GTAO, context, GTAOInspector,
				snapshot->m_GTAOUsesFormatFallback ? "R16 format fallback" : nullptr);
			ImGui::EndTable();
		}
		if (ImGui::CollapsingHeader("Shadows", ImGuiTreeNodeFlags_DefaultOpen) && BeginSettingsTable("ShadowSettings"))
		{
			DrawReadOnly("Shadows", snapshot->m_RuntimeAvailable ? (snapshot->m_ShadowSettings.m_Enable ? "ON" : "OFF")
				: "Unavailable", snapshot->m_Shadows, context, ShadowInspector);
			BeginRow("Shadow Method");
			ImGui::TextWrapped("%s", snapshot->m_RuntimeAvailable
				? (snapshot->m_ShadowSettings.m_EnablePCF ? "CSM + PCF" : "CSM") : "Unavailable");
			ImGui::TableSetColumnIndex(3);
			ImGui::TextDisabled("Read only");
			DrawInspect(context, ShadowInspector);
			ImGui::EndTable();
		}
		if (ImGui::CollapsingHeader("Temporal", ImGuiTreeNodeFlags_DefaultOpen) && BeginSettingsTable("TemporalSettings"))
		{
			DrawBlock("TAA", overrides ? &overrides->m_TemporalAA : nullptr,
				authoring.m_TemporalAA, requested.m_TemporalAA, resolved.m_TemporalAA.m_Enabled,
				snapshot->m_TemporalAA, context, TemporalInspector);
			ImGui::EndTable();
			ImGui::Text("History: %s", !snapshot->m_HistoryAvailable ? "Unavailable"
				: !snapshot->m_History.m_HasActiveHistory ? "No history"
				: snapshot->m_History.m_HistoryValid ? "Valid" : "Invalid");
		}
		if (ImGui::CollapsingHeader("Post Processing", ImGuiTreeNodeFlags_DefaultOpen) && BeginSettingsTable("PostProcessSettings"))
		{
			DrawBlock("Bloom", overrides ? &overrides->m_Bloom : nullptr,
				authoring.m_PostProcess.m_Bloom, requested.m_PostProcess.m_Bloom, resolved.m_PostProcess.m_Bloom.m_Enabled,
				snapshot->m_Bloom, context, PostProcessInspector);
			DrawReadOnly("Tone Mapping", "ACES fitted", snapshot->m_ToneMapping, context, PostProcessInspector);
			DrawScalar("Scene Pre-exposure", overrides ? &overrides->m_ScenePreExposure : nullptr,
				authoring.m_EnableScenePreExposure, requested.m_EnableScenePreExposure,
				requested.m_EnableScenePreExposure, snapshot->m_ScenePreExposure, context, PostProcessInspector);
			ImGui::EndTable();
			ImGui::TextDisabled("Scene storage scale: %.6g", resolved.m_Exposure.m_PreExposure);
		}
	}
}
