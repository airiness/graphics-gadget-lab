#include "Application/SelfTest/DevToolsViewProfileSelfTests.h"

#include "DevTools/DevToolsRuntime.h"
#include "DevTools/DevelopGui/DevelopGuiContext.h"
#include "DevTools/DevelopGui/Panels/RenderingSettingsPanel.h"
#include "GGLabRuntime/Diagnostics/DiagnosticsView.h"
#include "GGLabRuntime/Diagnostics/Snapshots/RenderingSettingsDiagnosticsSnapshot.h"
#include "GGLabRuntime/Graphics/PostProcess/ViewRenderSettings.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <array>
#include <memory>
#include <string>

namespace gglab
{
	namespace
	{
		bool ProfilesMatch(const ViewRenderProfile& left, const ViewRenderProfile& right) noexcept
		{
			const auto& leftGTAO = left.m_Lighting.m_GTAO;
			const auto& rightGTAO = right.m_Lighting.m_GTAO;
			const auto& leftBloom = left.m_PostProcess.m_Bloom;
			const auto& rightBloom = right.m_PostProcess.m_Bloom;
			return left.m_TemporalAA == right.m_TemporalAA &&
				left.m_EnableScenePreExposure == right.m_EnableScenePreExposure &&
				left.m_Lighting.m_ForwardPlus.m_Mode == right.m_Lighting.m_ForwardPlus.m_Mode &&
				left.m_Lighting.m_EnableAerialPerspective == right.m_Lighting.m_EnableAerialPerspective &&
				leftGTAO.m_Enabled == rightGTAO.m_Enabled &&
				leftGTAO.m_Radius == rightGTAO.m_Radius &&
				leftGTAO.m_FalloffStart == rightGTAO.m_FalloffStart &&
				leftGTAO.m_FalloffEnd == rightGTAO.m_FalloffEnd &&
				leftGTAO.m_Thickness == rightGTAO.m_Thickness &&
				leftGTAO.m_Power == rightGTAO.m_Power &&
				leftGTAO.m_DirectionCount == rightGTAO.m_DirectionCount &&
				leftGTAO.m_StepCount == rightGTAO.m_StepCount &&
				leftGTAO.m_DenoiseRadius == rightGTAO.m_DenoiseRadius &&
				leftGTAO.m_FinalAOFormatPreference == rightGTAO.m_FinalAOFormatPreference &&
				leftBloom.m_Enabled == rightBloom.m_Enabled &&
				leftBloom.m_Threshold == rightBloom.m_Threshold &&
				leftBloom.m_SoftKnee == rightBloom.m_SoftKnee &&
				leftBloom.m_Intensity == rightBloom.m_Intensity &&
				leftBloom.m_Scatter == rightBloom.m_Scatter &&
				leftBloom.m_MaxLevels == rightBloom.m_MaxLevels &&
				left.m_PostProcess.m_ToneMapping.m_Operator == right.m_PostProcess.m_ToneMapping.m_Operator;
		}

		class SettingsPanelDiagnostics final : public DiagnosticsView
		{
		public:
			RenderingSettingsDiagnosticsSnapshot m_Snapshot;
			uint32_t m_SettingsReads = 0;
			uint32_t m_OtherReads = 0;
			bool m_Available = true;
			std::vector<SnapshotProfile> GetProfiles() const override { return {}; }
		private:
			const void* GetSnapshotData(SnapshotId id) noexcept override
			{
				if (id == SnapshotIdOf<RenderingSettingsDiagnosticsSnapshot>)
				{
					++m_SettingsReads;
					return m_Available ? &m_Snapshot : nullptr;
				}
				++m_OtherReads;
				return nullptr;
			}
		};

		class SettingsNavigationProbe final : public DevelopGuiPanelBase
		{
		public:
			SettingsNavigationProbe(std::string_view path, uint32_t& draws, std::string& windowName) noexcept :
				m_Path(path), m_Draws(draws), m_WindowName(windowName) {}
			std::string_view GetPath() const noexcept override { return m_Path; }
			std::string_view GetTitle() const noexcept override { return m_Path; }
			void Draw(DevelopGuiContext&) noexcept override
			{
				++m_Draws;
				m_WindowName = ImGui::GetCurrentWindow()->Name;
			}
		private:
			std::string_view m_Path;
			uint32_t& m_Draws;
			std::string& m_WindowName;
		};

		void RunRenderingSettingsPanelChecks(SelfTestContext& context) noexcept
		{
			ImGuiContext* previousContext = ImGui::GetCurrentContext();
			ImGui::CreateContext();
			auto& io = ImGui::GetIO();
			io.IniFilename = nullptr;
			io.LogFilename = nullptr;
			io.DisplaySize = ImVec2(1400, 1200);
			io.DeltaTime = 1.0f / 60.0f;
			unsigned char* fontPixels = nullptr;
			int fontWidth = 0, fontHeight = 0;
			io.Fonts->GetTexDataAsRGBA32(&fontPixels, &fontWidth, &fontHeight);

			SettingsPanelDiagnostics diagnostics;
			auto& snapshot = diagnostics.m_Snapshot;
			snapshot.m_SettingsAvailable = snapshot.m_RuntimeAvailable = true;
			snapshot.m_DisplayViewId = RenderViewID::DebugCamera0;
			snapshot.m_Width = 1280;
			snapshot.m_Height = 720;
			snapshot.m_FrameSerial = 11;
			snapshot.m_AuthoringProfile.m_Lighting.m_GTAO.m_Radius = 2.5f;
			snapshot.m_AuthoringProfile.m_TemporalAA.m_MaxHistoryFeedback = 0.7f;
			snapshot.m_AuthoringProfile.m_TemporalAA.m_Enabled = true;
			snapshot.m_AuthoringProfile.m_PostProcess.m_Bloom.m_Scatter = 0.4f;
			snapshot.m_RequestedProfile = snapshot.m_AuthoringProfile;
			Camera camera(Camera::CreateInfo{});
			snapshot.m_ResolvedSettings = ResolveViewRenderSettings(snapshot.m_RequestedProfile, camera);
			snapshot.m_ForwardLighting = { ViewRenderFeatureState::Fallback, ViewRenderFeatureReason::DepthCoverageUnavailable };
			snapshot.m_ActualLightingMode = ForwardLightingMode::Legacy;
			snapshot.m_GTAO = { ViewRenderFeatureState::Unavailable, ViewRenderFeatureReason::CoreCapabilityUnavailable };
			snapshot.m_TemporalAA = { ViewRenderFeatureState::Active, ViewRenderFeatureReason::None };
			snapshot.m_HistoryAvailable = true;
			snapshot.m_History.m_HasActiveHistory = snapshot.m_History.m_HistoryValid = true;
			DevToolsRuntime devTools;
			auto& overrides = devTools.GetViewRenderSettingsOverrides();
			auto& registry = devTools.GetRegistry();
			constexpr std::array paths = { "Rendering/Forward+", "Rendering/Ambient Occlusion",
				"Rendering/Temporal AA", "Rendering/Post Processing", "Rendering/Shadows" };
			std::array<uint32_t, 5> draws{};
			std::array<std::string, 5> windowNames;
			for (size_t index = 0; index < paths.size(); ++index)
			{
				registry.RegisterPanel(std::make_unique<SettingsNavigationProbe>(paths[index], draws[index], windowNames[index]));
			}
			DevelopGuiContext gui{
				.m_Diagnostics = &diagnostics,
				.m_ViewRenderSettingsOverrides = &overrides,
				.m_PanelRegistry = &registry,
				.m_ActiveProfileName = "Settings Fixture",
				};
			RenderingSettingsPanel panel;
			const auto draw = [&](ImGuiID activation = 0)
			{
				if (activation) ImGui::ActivateItemByID(activation);
				ImGui::NewFrame();
				ImGui::SetNextWindowPos(ImVec2(20, 20));
				ImGui::SetNextWindowSize(ImVec2(1100, 1100));
				ImGui::Begin("RenderingSettingsFixture");
				ImGui::LogToBuffer();
				panel.Draw(gui);
				const std::string text = ImGui::GetCurrentContext()->LogBuffer.c_str();
				ImGui::LogFinish();
				ImGui::End();
				ImGui::EndFrame();
				return text;
			};
			GGLAB_UNUSED(draw());
			const auto text = draw();
			context.Check(text.find("Requested") != std::string::npos &&
				text.find("Legacy Fallback") != std::string::npos && text.find("Depth coverage unavailable") != std::string::npos &&
				text.find("Required capabilities unavailable") != std::string::npos && text.find("History: Valid") != std::string::npos,
				"Rendering Settings presents requested controls, runtime fallback reasons and independent history");
			context.Check(diagnostics.m_SettingsReads == 2 && diagnostics.m_OtherReads == 0,
				"The cockpit reads only the lightweight settings snapshot without Inspector or GPU services");
			const ImGuiID windowId = ImGui::FindWindowByName("RenderingSettingsFixture")->ID;
			const auto widget = [windowId](const char* table, const char* row, const char* label)
			{
				const ImGuiID tableId = ImHashStr(table, 0, windowId);
				const ImGuiID rowId = ImHashStr(row, 0, tableId);
				return ImHashStr(label, 0, rowId);
			};
			const ViewRenderProfile original = snapshot.m_AuthoringProfile;
			GGLAB_UNUSED(draw(widget("LightingSettings", "GTAO", "##Enabled")));
			GGLAB_UNUSED(draw(widget("TemporalSettings", "TAA", "##Enabled")));
			const auto pending = draw(widget("PostProcessSettings", "Bloom", "##Enabled"));
			context.Check(overrides.GetActiveCount() == 3 && !overrides.m_GTAO.m_Settings.m_Enabled &&
				!overrides.m_TemporalAA.m_Settings.m_Enabled && !overrides.m_Bloom.m_Settings.m_Enabled &&
				overrides.m_GTAO.m_Settings.m_Radius == 2.5f && overrides.m_TemporalAA.m_Settings.m_MaxHistoryFeedback == 0.7f &&
				overrides.m_Bloom.m_Settings.m_Scatter == 0.4f && ProfilesMatch(snapshot.m_AuthoringProfile, original),
				"Cockpit toggles seed complete authoring blocks without editing the source profile");
			context.Check(pending.find("Change pending") != std::string::npos && snapshot.m_TemporalAA.m_State == ViewRenderFeatureState::Active,
				"Pending toggles preserve the current frame's runtime observation until the next frame");
			overrides.m_GTAO.m_Settings.m_Radius = 4.0f;
			overrides.m_TemporalAA.m_Settings.m_MaxHistoryFeedback = 0.6f;
			overrides.m_Bloom.m_Settings.m_Intensity = 0.31f;
			GGLAB_UNUSED(draw(widget("LightingSettings", "GTAO", "##Enabled")));
			GGLAB_UNUSED(draw(widget("TemporalSettings", "TAA", "##Enabled")));
			GGLAB_UNUSED(draw(widget("PostProcessSettings", "Bloom", "##Enabled")));
			context.Check(overrides.GetActiveCount() == 3 && overrides.m_GTAO.m_Settings.m_Enabled &&
				overrides.m_TemporalAA.m_Settings.m_Enabled && overrides.m_Bloom.m_Settings.m_Enabled &&
				overrides.m_GTAO.m_Settings.m_Radius == 4.0f && overrides.m_TemporalAA.m_Settings.m_MaxHistoryFeedback == 0.6f &&
				overrides.m_Bloom.m_Settings.m_Intensity == 0.31f,
				"Cockpit toggles retain tuning already edited through the feature Inspectors");
			snapshot.m_AuthoringProfile.m_Lighting.m_GTAO.m_Radius = 6.5f;
			GGLAB_UNUSED(draw(widget("LightingSettings", "GTAO", "Reset")));
			context.Check(overrides.GetActiveCount() == 2 && !overrides.m_GTAO.m_IsActive &&
				devTools.ResolveViewRenderProfile(snapshot.m_AuthoringProfile).m_Lighting.m_GTAO.m_Radius == 6.5f,
				"A cockpit row reset inherits the current authoring profile while retaining sibling overrides");
			GGLAB_UNUSED(draw(widget("LightingSettings", "GTAO", "##Enabled")));
			context.Check(overrides.m_GTAO.m_Settings.m_Radius == 6.5f,
				"A later edit seeds the new authoring profile rather than an old Inspector or frame block");
			GGLAB_UNUSED(draw(widget("PostProcessSettings", "Scene Pre-exposure", "##Enabled")));
			context.Check(overrides.GetActiveCount() == 4 && overrides.m_ScenePreExposure == false,
				"Cockpit scalar controls keep explicit false intent");
			GGLAB_UNUSED(draw(widget("PostProcessSettings", "Scene Pre-exposure", "Reset")));
			context.Check(!overrides.m_ScenePreExposure && overrides.GetActiveCount() == 3,
				"A scalar row reset restores inheritance without clearing sibling overrides");
			GGLAB_UNUSED(draw(ImHashStr("Clear All Overrides", 0, windowId)));
			context.Check(overrides.GetActiveCount() == 0 &&
				ProfilesMatch(devTools.ResolveViewRenderProfile(snapshot.m_AuthoringProfile), snapshot.m_AuthoringProfile),
				"The cockpit Clear All button restores every current profile setting");
			const std::array inspectIds = {
				widget("LightingSettings", "Lighting Path", "Inspect"), widget("LightingSettings", "GTAO", "Inspect"),
				widget("TemporalSettings", "TAA", "Inspect"), widget("PostProcessSettings", "Bloom", "Inspect"),
				widget("ShadowSettings", "Shadows", "Inspect"),
				};
			for (size_t index = 0; index < paths.size(); ++index)
			{
				GGLAB_UNUSED(draw(inspectIds[index]));
				context.Check(registry.IsPanelOpen(paths[index]), "Cockpit Inspect opens the registered feature window");
			}
			ImGui::NewFrame();
			registry.DrawPanels(gui);
			ImGui::EndFrame();
			context.Check(draws[4] == 1 && gui.m_PanelRegistry == &registry && ImGui::GetCurrentContext()->NavWindow &&
				ImGui::GetCurrentContext()->NavWindow->ID == ImGui::FindWindowByName(windowNames[4].c_str())->ID,
				"Inspector navigation applies focus at the target Begin and restores the caller's registry context");
			ImGui::SetWindowCollapsed(windowNames[4].c_str(), true);
			GGLAB_UNUSED(registry.OpenPanel(paths[4]));
			ImGui::NewFrame();
			registry.DrawPanels(gui);
			ImGui::EndFrame();
			context.Check(draws[4] == 2 && !ImGui::FindWindowByName(windowNames[4].c_str())->Collapsed,
				"Inspect reopens an already open collapsed Inspector without creating another window");
			registry.Reset();
			context.Check(!registry.OpenPanel(paths[4]) && !registry.IsPanelOpen(paths[4]) && !registry.OpenPanel("Missing"),
				"Navigation rejects missing and reset registrations rather than creating phantom panels");
			diagnostics.m_Available = false;
			overrides.m_ScenePreExposure = false;
			const auto unavailable = draw();
			context.Check(unavailable.find("1 DevTools Overrides Active") != std::string::npos &&
				unavailable.find("Rendering settings are unavailable") != std::string::npos && unavailable.find("Requested") == std::string::npos,
				"Missing settings do not expose default controls or hide still-active session overrides");
			GGLAB_UNUSED(draw(ImHashStr("Clear All Overrides", 0, windowId)));
			context.Check(overrides.GetActiveCount() == 0 && diagnostics.m_OtherReads == 0,
				"Clear All remains usable without a settings publication and never captures detailed Inspectors");
			ImGui::DestroyContext();
			ImGui::SetCurrentContext(previousContext);
		}
	}

	void RunDevToolsViewProfileSelfTests(SelfTestContext& context) noexcept
	{
		ViewRenderProfile authoringProfile{};
		authoringProfile.m_Lighting.m_GTAO.m_Enabled = false;
		authoringProfile.m_Lighting.m_GTAO.m_Radius = 0.75f;
		authoringProfile.m_TemporalAA.m_Enabled = false;
		authoringProfile.m_PostProcess.m_Bloom.m_Enabled = false;
		const ViewRenderProfile originalAuthoringProfile = authoringProfile;
		DevToolsRuntime devTools;
		auto& overrides = devTools.GetViewRenderSettingsOverrides();
		context.Check(overrides.GetActiveCount() == 0 &&
			ProfilesMatch(devTools.ResolveViewRenderProfile(authoringProfile), originalAuthoringProfile),
			"A new DevTools session inherits every authoring setting");

		overrides.m_GTAO.m_Settings.m_Radius = 9.0f;
		overrides.m_TemporalAA.m_Settings.m_MaxHistoryFeedback = 0.5f;
		overrides.m_Bloom.m_Settings.m_Intensity = 0.5f;
		context.Check(overrides.GetActiveCount() == 0 &&
			ProfilesMatch(devTools.ResolveViewRenderProfile(authoringProfile), originalAuthoringProfile),
			"Inactive settings blocks neither override nor count as active intent");

		GTAOSettings gtaoSettings = originalAuthoringProfile.m_Lighting.m_GTAO;
		gtaoSettings.m_Enabled = true;
		gtaoSettings.m_Radius = 2.5f;
		gtaoSettings.m_FalloffStart = 0.25f;
		gtaoSettings.m_FalloffEnd = 2.0f;
		gtaoSettings.m_Thickness = 0.5f;
		gtaoSettings.m_Power = 2.0f;
		gtaoSettings.m_DirectionCount = 4;
		gtaoSettings.m_StepCount = 8;
		gtaoSettings.m_DenoiseRadius = 5;
		gtaoSettings.m_FinalAOFormatPreference = GTAOFinalAOFormatPreference::ForceR16Float;
		overrides.m_GTAO.Activate(gtaoSettings);
		ViewRenderProfile expectedProfile = originalAuthoringProfile;
		expectedProfile.m_Lighting.m_GTAO = gtaoSettings;
		context.Check(overrides.GetActiveCount() == 1 &&
			ProfilesMatch(devTools.ResolveViewRenderProfile(authoringProfile), expectedProfile) &&
			ProfilesMatch(authoringProfile, originalAuthoringProfile),
			"GTAO overrides its full block without changing authoring or unrelated features");

		TemporalAASettings temporalSettings = originalAuthoringProfile.m_TemporalAA;
		temporalSettings.m_Enabled = true;
		temporalSettings.m_DepthAbsoluteThreshold = 0.1f;
		temporalSettings.m_DepthRelativeThreshold = 0.04f;
		temporalSettings.m_MaxHistoryFeedback = 0.75f;
		temporalSettings.m_VelocityWeightScale = 0.1f;
		temporalSettings.m_LuminanceWeightScale = 0.25f;
		temporalSettings.m_NeighborhoodClampExpansion = 0.5f;
		overrides.m_TemporalAA.Activate(temporalSettings);
		expectedProfile.m_TemporalAA = temporalSettings;
		context.Check(overrides.GetActiveCount() == 2 &&
			ProfilesMatch(devTools.ResolveViewRenderProfile(authoringProfile), expectedProfile) &&
			ProfilesMatch(authoringProfile, originalAuthoringProfile),
			"TAA and GTAO overrides compose while leaving authoring unchanged");

		BloomSettings bloomSettings = originalAuthoringProfile.m_PostProcess.m_Bloom;
		bloomSettings.m_Enabled = true;
		bloomSettings.m_Threshold = 2.0f;
		bloomSettings.m_SoftKnee = 0.25f;
		bloomSettings.m_Intensity = 0.2f;
		bloomSettings.m_Scatter = 0.5f;
		bloomSettings.m_MaxLevels = 4;
		overrides.m_Bloom.Activate(bloomSettings);
		expectedProfile.m_PostProcess.m_Bloom = bloomSettings;
		context.Check(overrides.GetActiveCount() == 3 &&
			ProfilesMatch(devTools.ResolveViewRenderProfile(authoringProfile), expectedProfile) &&
			ProfilesMatch(authoringProfile, originalAuthoringProfile),
			"Bloom overrides every tuning parameter while preserving tone mapping and other features");

		overrides.ClearAll();
		overrides.m_ForwardLightingMode = ForwardLightingMode::Legacy;
		expectedProfile = originalAuthoringProfile;
		expectedProfile.m_Lighting.m_ForwardPlus.m_Mode = ForwardLightingMode::Legacy;
		context.Check(overrides.GetActiveCount() == 1 &&
			ProfilesMatch(devTools.ResolveViewRenderProfile(authoringProfile), expectedProfile),
			"Lighting-path override changes only the lighting path");

		overrides.ClearAll();
		overrides.m_ScenePreExposure = false;
		expectedProfile = originalAuthoringProfile;
		expectedProfile.m_EnableScenePreExposure = false;
		context.Check(overrides.GetActiveCount() == 1 &&
			ProfilesMatch(devTools.ResolveViewRenderProfile(authoringProfile), expectedProfile),
			"Scene pre-exposure can be overridden off without changing authoring exposure intent");
		overrides.m_ScenePreExposure = originalAuthoringProfile.m_EnableScenePreExposure;
		context.Check(overrides.GetActiveCount() == 1 &&
			ProfilesMatch(devTools.ResolveViewRenderProfile(authoringProfile), originalAuthoringProfile),
			"An explicit override equal to authoring remains active until reset");

		overrides.m_GTAO.Activate(gtaoSettings);
		overrides.m_TemporalAA.Activate(temporalSettings);
		overrides.m_Bloom.Activate(bloomSettings);
		overrides.m_ForwardLightingMode = ForwardLightingMode::Legacy;
		expectedProfile = originalAuthoringProfile;
		expectedProfile.m_Lighting.m_GTAO = gtaoSettings;
		expectedProfile.m_TemporalAA = temporalSettings;
		expectedProfile.m_PostProcess.m_Bloom = bloomSettings;
		expectedProfile.m_Lighting.m_ForwardPlus.m_Mode = ForwardLightingMode::Legacy;
		context.Check(overrides.GetActiveCount() == 5 &&
			ProfilesMatch(devTools.ResolveViewRenderProfile(authoringProfile), expectedProfile) &&
			ProfilesMatch(authoringProfile, originalAuthoringProfile),
			"All five overrides compose, counting blocks once and explicit false values as active");

		// A different authoring source must not silently clear session overrides or
		// re-seed the parameters of an already active settings block.
		ViewRenderProfile nextAuthoringProfile = originalAuthoringProfile;
		nextAuthoringProfile.m_Lighting.m_GTAO.m_Radius = 7.0f;
		nextAuthoringProfile.m_TemporalAA.m_MaxHistoryFeedback = 0.9f;
		nextAuthoringProfile.m_PostProcess.m_Bloom.m_Intensity = 0.4f;
		nextAuthoringProfile.m_EnableScenePreExposure = false;
		const ViewRenderProfile originalNextAuthoringProfile = nextAuthoringProfile;
		context.Check(overrides.GetActiveCount() == 5 &&
			ProfilesMatch(devTools.ResolveViewRenderProfile(nextAuthoringProfile), expectedProfile) &&
			ProfilesMatch(nextAuthoringProfile, originalNextAuthoringProfile),
			"DevTools session overrides survive authoring-profile switches without mutating either source");

		overrides.m_GTAO.Reset();
		expectedProfile.m_Lighting.m_GTAO = nextAuthoringProfile.m_Lighting.m_GTAO;
		context.Check(overrides.GetActiveCount() == 4 &&
			ProfilesMatch(devTools.ResolveViewRenderProfile(nextAuthoringProfile), expectedProfile),
			"Resetting one block restores the current authoring source and preserves sibling overrides");
		overrides.m_ScenePreExposure.reset();
		expectedProfile.m_EnableScenePreExposure = false;
		context.Check(overrides.GetActiveCount() == 3 &&
			ProfilesMatch(devTools.ResolveViewRenderProfile(nextAuthoringProfile), expectedProfile),
			"Resetting one scalar restores inheritance without clearing other overrides");

		overrides.ClearAll();
		context.Check(overrides.GetActiveCount() == 0 &&
			ProfilesMatch(devTools.ResolveViewRenderProfile(nextAuthoringProfile), originalNextAuthoringProfile),
			"Clear All restores every current authoring setting");
		nextAuthoringProfile.m_Lighting.m_GTAO.m_Radius = 3.0f;
		nextAuthoringProfile.m_PostProcess.m_Bloom.m_Intensity = 0.6f;
		nextAuthoringProfile.m_EnableScenePreExposure = true;
		context.Check(ProfilesMatch(devTools.ResolveViewRenderProfile(nextAuthoringProfile), nextAuthoringProfile),
			"Cleared overrides keep following subsequent authoring edits rather than a captured profile");

		overrides.m_Bloom.Activate(bloomSettings);
		overrides.m_ScenePreExposure = false;
		DevToolsRuntime nextDevToolsSession;
		context.Check(overrides.GetActiveCount() == 2 &&
			nextDevToolsSession.GetViewRenderSettingsOverrides().GetActiveCount() == 0 &&
			ProfilesMatch(nextDevToolsSession.ResolveViewRenderProfile(nextAuthoringProfile), nextAuthoringProfile),
			"Session overrides are instance-owned and never inherited by a new DevTools session");
		devTools.Reset();
		context.Check(overrides.GetActiveCount() == 0 &&
			ProfilesMatch(devTools.ResolveViewRenderProfile(nextAuthoringProfile), nextAuthoringProfile) &&
			ProfilesMatch(authoringProfile, originalAuthoringProfile),
			"DevTools reset clears rendering overrides without modifying authoring state");
		RunRenderingSettingsPanelChecks(context);
	}
}
