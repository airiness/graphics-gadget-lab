#include "DevToolsViewProfileSelfTests.h"

#include "DevTools/DevToolsRuntime.h"
#include "DevTools/DevelopGui/DevelopGuiContext.h"
#include "DevTools/DevelopGui/DevelopGuiRegistry.h"
#include "DevTools/DevelopGui/Panels/RenderingSettingsPanel.h"
#include "DevTools/DevelopGui/Panels/TemporalAAInspectorPanel.h"
#include "DevTools/DevelopGui/Panels/GTAOInspectorPanel.h"
#include "DevTools/DevelopGui/Panels/PostProcessInspectorPanel.h"
#include "DevTools/DevelopGui/Panels/GIViewerPanel.h"
#include "DevTools/DevelopGui/Panels/LabPanel.h"
#include "GGLabRuntime/Graphics/PostProcess/PostProcessPreviewControlBase.h"
#include "GGLabRuntime/Graphics/PostProcess/PostProcessPreviewViewBase.h"
#include "Parameters/ViewControlSnapshot.h"
#include "Parameters/HostViewParameters.h"
#include "Parameters/ViewControlActions.h"
#include "Capture/FrameCaptureCoordinator.h"
#include "Capture/GIProbeInspectionControl.h"
#include "Capture/GIReceiverInspectionControl.h"
#include "Capture/GIProbePickingControl.h"
#include "GGLabRuntime/Diagnostics/DiagnosticsView.h"
#include "GGLabRuntime/Diagnostics/Snapshots/RenderingSettingsDiagnosticsSnapshot.h"
#include "GGLabRuntime/Graphics/ViewRenderSettings.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

namespace gglab
{
	namespace
	{
		class GIPreviewProbe final : public PostProcessPreviewViewBase, public PostProcessPreviewControlBase
		{
		public:
			PostProcessPreviewDiagnostics m_Preview{};
			uint32_t m_Requests = 0;
			PostProcessPreviewDiagnostics GetPostProcessPreviewDiagnostics(PostProcessPreviewChannel) const noexcept override
			{
				return m_Preview;
			}
			void SetPostProcessPreviewSelection(PostProcessDebugSelection selection, PostProcessPreviewChannel) noexcept override
			{
				m_Preview.m_Selected = selection;
			}
			void SetPostProcessPreviewExposureEV(float exposure, PostProcessPreviewChannel) noexcept override
			{
				m_Preview.m_ExposureEV = exposure;
			}
			void RequestPostProcessPreview(PostProcessPreviewChannel) noexcept override { ++m_Requests; }
		};

		class GIInspectionProbe final : public GIProbeInspectionControlBase
		{
		public:
			std::optional<GIProbeInspectionRequest> m_Request;
			std::shared_ptr<const GIProbeInspectionResult> m_Result;
			std::string m_Status = "Fixture inspection";
			uint32_t m_Cancellations = 0;
			bool Inspect(GIProbeInspectionRequest request) noexcept override
			{
				m_Request = request;
				return true;
			}
			void Cancel() noexcept override
			{
				++m_Cancellations;
				m_Result.reset();
			}
			bool IsPending() const noexcept override { return false; }
			std::shared_ptr<const GIProbeInspectionResult> GetResult() const noexcept override { return m_Result; }
			const std::string& GetStatus() const noexcept override { return m_Status; }
		};

		class GIReceiverProbe final : public GIReceiverInspectionControlBase
		{
		public:
			std::optional<GIReceiverInspectionRequest> m_Request;
			std::shared_ptr<const GIReceiverInspectionResult> m_Result;
			std::string m_Status = "Fixture receiver";
			std::string m_ImagePath;
			uint32_t m_Cancellations = 0;
			bool Inspect(GIReceiverInspectionRequest request) noexcept override
			{
				m_Request = request;
				return true;
			}
			void Cancel() noexcept override
			{
				++m_Cancellations;
				m_Result.reset();
			}
			bool IsPending() const noexcept override { return false; }
			std::shared_ptr<const GIReceiverInspectionResult> GetResult() const noexcept override { return m_Result; }
			const std::string& GetStatus() const noexcept override { return m_Status; }
			const std::string& GetImagePath() const noexcept override { return m_ImagePath; }
		};

		class GIPickingProbe final : public GIProbePickingControlBase
		{
		public:
			GIProbePickingDisplay m_Display;
			std::optional<GIProbePickingRequest> m_Request;
			std::shared_ptr<const GIProbePickingResult> m_Result;
			std::string m_Status = "Fixture picking";
			uint32_t m_Requests = 0;
			uint32_t m_Cancellations = 0;
			const GIProbePickingDisplay& GetDisplay() const noexcept override { return m_Display; }
			bool Pick(GIProbePickingRequest request) noexcept override
			{
				m_Request = std::move(request);
				++m_Requests;
				return true;
			}
			void Cancel() noexcept override { ++m_Cancellations; }
			std::shared_ptr<const GIProbePickingResult> GetResult() const noexcept override { return m_Result; }
			const std::string& GetStatus() const noexcept override { return m_Status; }
		};

		void RunGIProbePickingGuiChecks(SelfTestContext& context) noexcept
		{
			auto* previous = ImGui::GetCurrentContext();
			ImGui::CreateContext();
			auto& io = ImGui::GetIO();
			io.IniFilename = io.LogFilename = nullptr;
			io.DisplaySize = ImVec2(1600, 1000);
			io.ConfigInputTrickleEventQueue = false;
			unsigned char* pixels = nullptr;
			int width = 0, height = 0;
			io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
			auto source = std::make_shared<GIFieldSummary>();
			source->m_Field = { .m_Generation = 7, .m_SceneRevision = 4 };
			auto& display = source->m_Visualization;
			display.m_SettingsRevision = 2;
			display.m_Settings.m_Enabled = true;
			display.m_Available = display.m_PickingAvailable = true;
			display.m_Observation = GIObservationStamp{ .m_ProducingFrame = 100 };
			display.m_View = { .m_Revision = 3, .m_RenderExtent = { 1600, 1000 },
				.m_DisplayExtent = { 1600, 1000 }, .m_TemporalAA = true,
				.m_ProjectionSupported = true, .m_Valid = true };
			auto current = *source;
			current.m_Visualization.m_View.m_TemporalFrameIndex = 23;
			current.m_Visualization.m_View.m_JitterPixels = { .25f, -.25f };
			DevToolsRuntime tools;
			auto& overrides = tools.GetViewRenderSettingsOverrides();
			overrides.m_GIProbes = display.m_Settings;
			GIPickingProbe picking;
			picking.m_Display = { source, 1, "gglab.lab.test" };
			GIInspectionProbe numeric;
			DevelopGuiContext gui{ .m_ViewRenderSettingsOverrides = &overrides, .m_GIFieldSummary = &current,
				.m_GIProbeInspection = &numeric, .m_GIProbePicking = &picking,
				.m_AbsolutePointer = true, .m_ContentId = "gglab.lab.test" };
			GIViewerPanel panel;
			const auto draw = [&](ImGuiID activation = 0, bool captured = false, bool contents = true)
				{
					if (activation)
					{
						ImGui::ActivateItemByID(activation);
					}
					ImGui::NewFrame();
					if (contents)
					{
						ImGui::Begin("PickingFixture");
						panel.Draw(gui);
						ImGui::End();
					}
					io.WantCaptureMouse = captured;
					panel.OnFrame(gui);
					ImGui::EndFrame();
				};
			draw();
			draw(ImHashStr("Pick probes with left click", 0, ImHashStr("PickingFixture")));
			const auto click = [&](bool captured = false)
				{
					io.AddMousePosEvent(100, 800);
					io.AddMouseButtonEvent(0, false);
					draw();
					io.AddMouseButtonEvent(0, true);
					draw(0, captured);
				};
			click(true);
			gui.m_AbsolutePointer = false;
			click();
			context.Check(picking.m_Requests == 0, "GUI picking ignores captured and relative-pointer clicks");
			gui.m_AbsolutePointer = true;
			click();
			context.Check(picking.m_Request && picking.m_Request->m_SourceFrame == 100 &&
				picking.m_Request->m_View == display.m_View && picking.m_Request->m_Pixel == std::array<uint32_t, 2>{ 100, 800 },
				"Mouse clicks freeze the previous submitted display rather than the planned jitter phase");
			if (picking.m_Request)
			{
				const auto earlier = *picking.m_Request;
				click();
				auto result = std::make_shared<GIProbePickingResult>();
				result->m_Request = earlier;
				result->m_Validity = GIProbePickingValidity::Hit;
				result->m_Coordinate = std::array<uint32_t, 3>{ 1, 2, 3 };
				picking.m_Result = result;
				draw(0, false, false);
				context.Check(!overrides.m_GIProbes.m_Selection, "A late earlier token cannot overwrite the latest GUI selection");
				result->m_Request = *picking.m_Request;
				draw(0, false, false);
				context.Check(overrides.m_GIProbes.m_Selection &&
					overrides.m_GIProbes.m_Selection->m_Coordinate == *result->m_Coordinate && numeric.m_Cancellations == 0,
					"A matching result applies while collapsed without cancelling independent numeric inspection");
				picking.m_Result.reset();
				click();
				const auto before = picking.m_Cancellations;
				gui.m_DisplayDpiScale = 1.25f;
				draw(0, false, false);
				context.Check(picking.m_Cancellations == before + 1, "DPI changes invalidate an in-flight GUI token");
				gui.m_DisplayDpiScale = 1;
				click();
				const auto closing = picking.m_Cancellations;
				panel.OnClosed(gui);
				context.Check(picking.m_Cancellations == closing + 1 && !overrides.m_GIProbes.m_Selection,
					"Closing the panel invalidates its picking token and releases its GUI selection");
			}
			ImGui::DestroyContext();
			ImGui::SetCurrentContext(previous);
		}

		void RunGIViewerPanelChecks(SelfTestContext& context) noexcept
		{
			ImGuiContext* previousContext = ImGui::GetCurrentContext();
			ImGui::CreateContext();
			auto& io = ImGui::GetIO();
			io.IniFilename = nullptr;
			io.LogFilename = nullptr;
			io.DisplaySize = ImVec2(1600, 1000);
			unsigned char* pixels = nullptr;
			int width = 0, height = 0;
			io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
			GIPreviewProbe probe;
			probe.m_Preview.m_Selected.m_Tap = PostProcessDebugTap::GIIrradiance;
			probe.m_Preview.m_Published = probe.m_Preview.m_Selected;
			probe.m_Preview.m_HasPublished = true;
			probe.m_Preview.m_GISnapshot = { .m_Generation = 7, .m_Samples = 32,
				.m_TargetSamples = 64, .m_MaximumDepth = 4 };
			DevelopGuiContext gui{ .m_PostProcessPreview = &probe, .m_PostProcessPreviewControl = &probe };
			GIViewerPanel panel;
			const auto draw = [&](bool toggleFreeze = false)
				{
					if (toggleFreeze)
					{
						ImGui::ActivateItemByID(ImHashStr("Freeze preview", 0, ImHashStr("GIViewerFixture")));
					}
					ImGui::NewFrame();
					ImGui::Begin("GIViewerFixture");
					ImGui::LogToBuffer();
					panel.Draw(gui);
					const std::string text = ImGui::GetCurrentContext()->LogBuffer.c_str();
					ImGui::LogFinish();
					ImGui::End();
					ImGui::EndFrame();
					return text;
				};
			GGLAB_UNUSED(draw());
			const auto live = draw();
			context.Check(probe.m_Requests == 2 && live.find("samples 32 / 64") != std::string::npos && live.find("depth 4") != std::string::npos,
				"The GI Viewer requests live updates and labels submitted estimator provenance");
			const auto frozen = draw(true);
			GGLAB_UNUSED(draw());
			context.Check(probe.m_Requests == 2 && frozen.find("Frozen") != std::string::npos,
				"Freezing the real Viewer checkbox stops preview demand across later draws");
			const auto resumed = draw(true);
			context.Check(probe.m_Requests == 3 && resumed.find("Live") != std::string::npos,
				"Unfreezing resumes preview demand without changing reference sampling controls");
			context.Check(resumed.find("No preview yet") != std::string::npos &&
				ImGui::FindWindowByName("GIViewerFixture")->Size.x >= 760 &&
				ImGui::FindWindowByName("GIViewerFixture")->Size.y >= 660,
				"Unavailable GI has an explanation and the first-use Viewer leaves room for its image");
			DevToolsRuntime displayTools;
			auto& displayOverrides = displayTools.GetViewRenderSettingsOverrides();
			gui.m_ViewRenderSettingsOverrides = &displayOverrides;
			ImGui::ActivateItemByID(ImHashStr("Probe array", 0, ImHashStr("GIViewerFixture")));
			GGLAB_UNUSED(draw());
			ImGui::ActivateItemByID(ImHashStr("Show probe spheres", 0, ImHashStr("GIViewerFixture")));
			GGLAB_UNUSED(draw());
			context.Check(displayOverrides.m_GIProbes.m_Enabled && displayOverrides.GetActiveCount() == 1,
				"The real Viewer checkbox enables only its GUI probe display block");
			Camera displayCamera(Camera::CreateInfo{});
			displayOverrides.m_GIProbes.m_Mode = GIProbeVisualizationMode::Irradiance;
			displayOverrides.m_GIProbes.m_PreviewExposureEV = 1.5f;
			displayOverrides.m_GIProbes.m_DepthMode = GIProbeDepthMode::SceneDepth;
			const auto displaySettings = displayTools.CaptureFrameSettings();
			auto resolvedDisplay = ResolveApplicationViewSettings({}, displayCamera, displaySettings);
			context.Check(resolvedDisplay.m_ToolingBlocks.m_GIProbes && resolvedDisplay.m_View.m_GIProbes.m_Enabled &&
				resolvedDisplay.m_View.m_GIProbes.m_Mode == GIProbeVisualizationMode::Irradiance &&
				resolvedDisplay.m_View.m_GIProbes.m_PreviewExposureEV == 1.5f &&
				resolvedDisplay.m_View.m_GIProbes.m_DepthMode == GIProbeDepthMode::SceneDepth,
				"GUI probe settings reach Runtime frame settings with tooling attribution");
			const std::array changeDisplay{
				LabParameterValue{ LabParameterId("view.gi_probes.mode"), int32_t(2) },
				LabParameterValue{ LabParameterId("view.gi_probes.change_normalized"), false },
				LabParameterValue{ LabParameterId("view.gi_probes.change_range"), 0.25f },
				LabParameterValue{ LabParameterId("view.gi_probes.change_blocks"), true } };
			ApplyHostViewParameters(changeDisplay, resolvedDisplay.m_View, true);
			context.Check(resolvedDisplay.m_View.m_GIProbes.m_Mode == GIProbeVisualizationMode::Change &&
				!resolvedDisplay.m_View.m_GIProbes.m_ChangeNormalized &&
				resolvedDisplay.m_View.m_GIProbes.m_ChangeRange == 0.25f && resolvedDisplay.m_View.m_GIProbes.m_ChangeBlocks,
				"Host Change controls reach the same Runtime settings as the GUI");
			const std::array sessionDisplay{
				LabParameterValue{ LabParameterId("view.gi_probes.enabled"), true },
				LabParameterValue{ LabParameterId("view.gi_probes.budget"), uint32_t(17) } };
			ApplyHostViewParameters(sessionDisplay, resolvedDisplay.m_View, true);
			context.Check(resolvedDisplay.m_View.m_GIProbes.m_Budget == 17 &&
				displayOverrides.m_GIProbes.m_Budget == 4096,
				"Session display settings override GUI intent without modifying its owned request");
			ImGui::ActivateItemByID(ImHashStr("Probe array", 0, ImHashStr("GIViewerFixture")));
			GGLAB_UNUSED(draw());
			GIFieldSummary fieldSummary{ .m_Field = { .m_Generation = 73, .m_RefreshEpoch = 2,
				.m_EpochOriginUpdate = 80 }, .m_Counts = { 4, 4, 4 }, .m_InspectionAvailable = true,
				.m_ReceiverInspectionAvailable = true, .m_ReceiverMarkersAvailable = true };
			GIInspectionProbe inspection;
			gui.m_GIFieldSummary = &fieldSummary;
			gui.m_GIProbeInspection = &inspection;
			// Exercise full Coastal counts through the actual panel and a smaller window.
			displayOverrides.m_GIProbes.m_Budget = 2048;
			fieldSummary.m_Counts = { 33, 10, 39 };
			fieldSummary.m_Visualization.m_Settings = displayOverrides.m_GIProbes;
			fieldSummary.m_Visualization.m_CompletedObservation = GIObservationStamp{
				.m_Field = fieldSummary.m_Field, .m_ProducingFrame = 101 };
			fieldSummary.m_Visualization.m_CompletedCounts = {
				.m_Enumerated = 12870, .m_AfterSlice = 12870, .m_AfterClassification = 12870,
				.m_AfterDistance = 12870, .m_AfterFrustum = 8000,
				.m_Rendered = 2048, .m_BudgetDropped = 5952 };
			ImGui::ActivateItemByID(ImHashStr("Probe array", 0, ImHashStr("GIViewerFixture")));
			const auto budgetText = draw();
			context.Check(budgetText.find("2048 / 12870 displayed, 5952 budget drops") != std::string::npos &&
				budgetText.find("Scene Depth can hide the retained first-N probes") != std::string::npos,
				"Full Coastal counts distinguish display-budget truncation from filtering");
			ImGui::SetWindowSize("GIViewerFixture", ImVec2(640, 400));
			const auto resizedText = draw();
			context.Check(resizedText.find("5952 budget drops") != std::string::npos &&
				displayOverrides.m_GIProbes.m_Budget == 2048,
				"Resizing the Viewer retains its budget and truncation explanation");
			ImGui::SetWindowSize("GIViewerFixture", ImVec2(760, 660));
			auto completedChange = std::make_shared<GIFieldChangeObservation>();
			auto changeMeasurement = std::make_shared<GIFieldReadinessMeasurement>();
			changeMeasurement->m_Counts = { 4, 4, 4 };
			changeMeasurement->m_WarmingUpdates = 4;
			changeMeasurement->m_RequiredQuietUpdates = 30;
			changeMeasurement->m_Readiness.m_State = GIFieldReadinessState::Settling;
			changeMeasurement->m_Readiness.m_Threshold = 0.3f;
			changeMeasurement->m_Probes.resize(64, { 0.2f, true });
			changeMeasurement->m_Blocks.push_back({ .m_ActiveProbes = 64,
				.m_Percentile95 = 0.2f, .m_Maximum = 0.4f, .m_QuietUpdates = 17,
				.m_Blockers = 8, .m_WarmingMet = true, .m_CurrentQuiet = true });
			completedChange->m_Observation = { .m_Field = fieldSummary.m_Field,
				.m_ProducingFrame = 113, .m_FieldUpdate = 100, .m_ReadinessUpdate = 20 };
			completedChange->m_Measurement = changeMeasurement;
			fieldSummary.m_Visualization.m_ChangeObservation = completedChange;
			fieldSummary.m_Visualization.m_Settings.m_Mode = GIProbeVisualizationMode::Change;
			const auto changeText = draw();
			context.Check(changeText.find("Completed frame 113") != std::string::npos &&
				changeText.find("17 / 30") != std::string::npos && changeText.find("hold") != std::string::npos &&
				changeText.find("irradiance change only") != std::string::npos &&
				changeMeasurement->m_Blocks[0].m_QuietUpdates == 17,
				"The real Change panel displays the completed source and incomplete hold without advancing readiness");
			fieldSummary.m_Visualization.m_ChangeObservation.reset();
			fieldSummary.m_Readiness.m_Enabled = true;
			const auto pendingChange = draw();
			context.Check(pendingChange.find("no completed measurement for this field/refresh epoch") != std::string::npos,
				"A missing completed observation is displayed as pending rather than zero change");
			fieldSummary.m_Readiness.m_Enabled = false;
			context.Check(draw().find("readiness measurements disabled") != std::string::npos,
				"The real Change panel distinguishes an explicitly disabled measurement owner");
			fieldSummary.m_Visualization.m_Settings.m_Mode = GIProbeVisualizationMode::Irradiance;
			ImGui::ActivateItemByID(ImHashStr("Probe array", 0, ImHashStr("GIViewerFixture")));
			GGLAB_UNUSED(draw());
			fieldSummary.m_Counts = { 4, 4, 4 };
			GGLAB_UNUSED(draw());
			ImGui::ActivateItemByID(ImHashStr("Inspect probe", 0, ImHashStr("GIViewerFixture")));
			const auto inspectionText = draw();
			context.Check(inspection.m_Request && inspection.m_Request->m_ExpectedGeneration == 73 &&
				inspection.m_Request->m_ExpectedRefreshEpoch == 2 &&
				inspectionText.find("No completed readiness measurement") != std::string::npos,
				"GUI inspection binds the displayed field identity and keeps pending measurement absence explicit");
			context.Check(displayOverrides.m_GIProbes.m_Selection &&
				displayOverrides.m_GIProbes.m_Selection->m_ExpectedGeneration == 73,
				"Inspecting a coordinate selects the same field-bound diagnostic highlight");
			ImGui::ActivateItemByID(ImHashStr("Cancel / clear", 0, ImHashStr("GIViewerFixture")));
			GGLAB_UNUSED(draw());
			context.Check(!displayOverrides.m_GIProbes.m_Selection && !inspection.m_Result,
				"Clearing the numeric inspector releases its GUI selection and retained result");
			ImGui::ActivateItemByID(ImHashStr("Select / highlight probe", 0, ImHashStr("GIViewerFixture")));
			GGLAB_UNUSED(draw());
			fieldSummary.m_Field.m_Generation = 74;
			GGLAB_UNUSED(draw());
			context.Check(displayOverrides.m_GIProbes.m_Selection &&
				displayOverrides.m_GIProbes.m_Selection->m_ExpectedGeneration == 73,
				"A field replacement cannot silently rebind the GUI selection to a new generation");
			fieldSummary.m_Field.m_Generation = 73;
			auto historical = std::make_shared<GIProbeInspectionResult>();
			historical->m_Observation.m_Field.m_Generation = 72;
			historical->m_Observation.m_ProducingFrame = 913;
			historical->m_Observation.m_FieldUpdate = 85;
			inspection.m_Result = historical;
			const auto historicalText = draw();
			context.Check(historicalText.find("Historical field/selection") != std::string::npos &&
				historicalText.find("frame 913") != std::string::npos,
				"Retained GPU values show their historical identity after a field replacement");
			GIReceiverProbe receiver;
			gui.m_GIReceiverInspection = &receiver;
			fieldSummary.m_ReceiverView = { .m_Revision = 3, .m_RenderExtent = { 320, 200 },
				.m_DisplayExtent = { 320, 200 }, .m_ReceiversAvailable = true, .m_Supported = true };
			ImGui::ActivateItemByID(ImHashStr("Receiver contribution inspection", 0, ImHashStr("GIViewerFixture")));
			GGLAB_UNUSED(draw());
			ImGui::ActivateItemByID(ImHashStr("Inspect receiver", 0, ImHashStr("GIViewerFixture")));
			GGLAB_UNUSED(draw());
			context.Check(receiver.m_Request && receiver.m_Request->m_ExpectedGeneration == 73 &&
				receiver.m_Request->m_ExpectedViewRevision == 3 && receiver.m_Request->m_Markers,
				"The receiver GUI binds the displayed logical field/view and requests a paired marker capture");
			receiver.m_Request.reset();
			fieldSummary.m_ReceiverMarkersAvailable = false;
			ImGui::ActivateItemByID(ImHashStr("Inspect receiver", 0, ImHashStr("GIViewerFixture")));
			const auto unavailableMarkers = draw();
			context.Check(!receiver.m_Request && unavailableMarkers.find("X-ray markers are unavailable") != std::string::npos,
				"The Viewer disables paired-marker inspection when only numeric querying is available");
			ImGui::ActivateItemByID(ImHashStr("Capture X-ray contribution markers", 0, ImHashStr("GIViewerFixture")));
			GGLAB_UNUSED(draw());
			ImGui::ActivateItemByID(ImHashStr("Inspect receiver", 0, ImHashStr("GIViewerFixture")));
			GGLAB_UNUSED(draw());
			context.Check(receiver.m_Request && !receiver.m_Request->m_Markers,
				"Disabling unavailable markers leaves numeric receiver inspection usable");
			receiver.m_Request.reset();
			fieldSummary.m_ReceiverInspectionAvailable = false;
			ImGui::ActivateItemByID(ImHashStr("Inspect receiver", 0, ImHashStr("GIViewerFixture")));
			GGLAB_UNUSED(draw());
			context.Check(!receiver.m_Request, "The Viewer disables inspection when the receiver query is unavailable");
			fieldSummary.m_ReceiverInspectionAvailable = true;
			fieldSummary.m_ReceiverMarkersAvailable = true;
			auto zero = std::make_shared<GIReceiverInspectionResult>();
			zero->m_Validity = GIReceiverInspectionValidity::NoValidSupport;
			zero->m_CageEvaluated = true;
			zero->m_Observation.m_Field = fieldSummary.m_Field;
			zero->m_Observation.m_ProducingFrame = 123;
			zero->m_View.m_Revision = 2;
			receiver.m_Result = zero;
			const auto receiverText = draw();
			context.Check(receiverText.find("Historical field/view/selection") != std::string::npos &&
				receiverText.find("Normalization unavailable") != std::string::npos,
				"Receiver tables retain historical source identity and explain zero-weight normalization");
			DevelopGuiRegistry registry;
			registry.RegisterPanel(std::make_unique<GIViewerPanel>());
			const auto drawRegistry = [&]()
				{
					ImGui::NewFrame();
					registry.DrawMenuBar();
					registry.DrawPanels(gui);
					ImGui::EndFrame();
				};
			GGLAB_UNUSED(registry.OpenPanel("Rendering/GI Viewer"));
			drawRegistry();
			drawRegistry();
			ImGuiWindow* viewerWindow = nullptr;
			for (auto* window : ImGui::GetCurrentContext()->Windows)
			{
				if (std::string_view(window->Name).starts_with("GI Viewer##"))
				{
					viewerWindow = window;
					break;
				}
			}
			context.Check(viewerWindow != nullptr, "The registered GI Viewer creates its real window");
			if (viewerWindow)
			{
				displayOverrides.m_GIProbes.m_Enabled = true;
				const auto probeCancellations = inspection.m_Cancellations;
				const auto receiverCancellations = receiver.m_Cancellations;
				ImGui::SetWindowCollapsed(viewerWindow, true);
				drawRegistry();
				context.Check(inspection.m_Cancellations == probeCancellations &&
					receiver.m_Cancellations == receiverCancellations,
					"Collapsing the GI Viewer preserves its inspection requests");
				ImGui::SetWindowCollapsed(viewerWindow, false);
				drawRegistry();
				ImGui::ActivateItemByID(viewerWindow->GetID("#CLOSE"));
				drawRegistry();
				context.Check(!registry.IsPanelOpen("Rendering/GI Viewer") &&
					inspection.m_Cancellations == probeCancellations + 1 &&
					receiver.m_Cancellations == receiverCancellations + 1 &&
					!inspection.m_Result && !receiver.m_Result,
					"The GI Viewer close button cancels both owned inspections in the closing frame");
				context.Check(!displayOverrides.m_GIProbes.m_Enabled && !displayOverrides.m_GIProbes.m_Selection,
					"Closing the Viewer releases its GUI probe display request");
				resolvedDisplay = ResolveApplicationViewSettings({}, displayCamera, displayTools.CaptureFrameSettings());
				ApplyHostViewParameters(sessionDisplay, resolvedDisplay.m_View, true);
				context.Check(!resolvedDisplay.m_ToolingBlocks.m_GIProbes &&
					resolvedDisplay.m_View.m_GIProbes.m_Enabled && resolvedDisplay.m_View.m_GIProbes.m_Budget == 17,
					"Closing the GUI request preserves independent session display intent");
				drawRegistry();
				context.Check(inspection.m_Cancellations == probeCancellations + 1 &&
					receiver.m_Cancellations == receiverCancellations + 1,
					"A closed GI Viewer does not repeatedly cancel requests on later frames");
				GGLAB_UNUSED(registry.OpenPanel("Rendering/GI Viewer"));
				drawRegistry();
				const auto menuBarId = ImHashStr("##MenuBar", 0, ImHashStr("##MainMenuBar"));
				ImGui::ActivateItemByID(ImHashStr("Rendering", 0, menuBarId));
				drawRegistry();
				auto* menuWindow = ImGui::FindWindowByName("Rendering###Menu_00");
				context.Check(menuWindow && menuWindow->Active, "The Rendering menu opens through its real menu item");
				if (menuWindow && menuWindow->Active)
				{
					ImGui::ActivateItemByID(menuWindow->GetID(viewerWindow->Name));
					drawRegistry();
					context.Check(!registry.IsPanelOpen("Rendering/GI Viewer") &&
						inspection.m_Cancellations == probeCancellations + 2 &&
						receiver.m_Cancellations == receiverCancellations + 2,
						"Closing the GI Viewer from the menu notifies it before the registry skips its draw");
				}
			}
			gui.m_ViewRenderSettingsOverrides = nullptr;
			LabSessionLog log;
			LabSnapshot snapshot;
			snapshot.m_ActiveLabName = "GI Reference";
			snapshot.m_IsHostActive = true;
			const auto drawLog = [&]()
				{
					ImGui::NewFrame();
					ImGui::SetNextWindowSize(ImVec2(500, 600));
					ImGui::Begin("LabLogFixture");
					log.Draw(snapshot);
					const float controlY = ImGui::GetCursorPosY();
					ImGui::Button("Parameter below log");
					ImGui::End();
					ImGui::EndFrame();
					return controlY;
				};
			snapshot.m_State = LabSnapshotRunState::Ready;
			GGLAB_UNUSED(drawLog());
			const float readyY = drawLog();
			snapshot.m_State = LabSnapshotRunState::WarmingUp;
			snapshot.m_WarmupFramesRemaining = 30;
			const float warmupY = drawLog();
			snapshot.m_HasPendingSession = true;
			snapshot.m_LoadingDetail = std::string(1000, 'x');
			for (uint32_t entry = 0; entry < 100; ++entry)
			{
				log.Append(std::to_string(entry));
			}
			context.Check(readyY == warmupY && readyY == drawLog(),
				"Warm-up, long loading details and scrolling history cannot shift controls below the Lab log");
			ImGui::DestroyContext();
			ImGui::SetCurrentContext(previousContext);
		}

		bool ProfilesMatch(const ViewRenderProfile& left, const ViewRenderProfile& right) noexcept
		{
			const auto& leftGTAO = left.m_Lighting.m_GTAO;
			const auto& rightGTAO = right.m_Lighting.m_GTAO;
			const auto& leftBloom = left.m_PostProcess.m_Bloom;
			const auto& rightBloom = right.m_PostProcess.m_Bloom;
			return left.m_TemporalAA == right.m_TemporalAA &&
				left.m_EnableScenePreExposure == right.m_EnableScenePreExposure &&
				left.m_Lighting.m_EnableAerialPerspective == right.m_Lighting.m_EnableAerialPerspective &&
				leftGTAO.m_Enabled == rightGTAO.m_Enabled &&
				leftGTAO.m_Radius == rightGTAO.m_Radius &&
				leftGTAO.m_FalloffStart == rightGTAO.m_FalloffStart &&
				leftGTAO.m_FalloffEnd == rightGTAO.m_FalloffEnd &&
				leftGTAO.m_Power == rightGTAO.m_Power &&
				leftGTAO.m_DirectionCount == rightGTAO.m_DirectionCount &&
				leftGTAO.m_StepCount == rightGTAO.m_StepCount &&
				leftGTAO.m_DenoiseRadius == rightGTAO.m_DenoiseRadius &&
				leftGTAO.m_TemporalAccumulation == rightGTAO.m_TemporalAccumulation &&
				leftGTAO.m_TemporalMaxSamples == rightGTAO.m_TemporalMaxSamples &&
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
				m_Path(path), m_Draws(draws), m_WindowName(windowName)
			{
			}
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

		class ReleaseControlProbe final : public ViewControlActionsBase
		{
		public:
			ViewOverrideReleaseStatus m_Status;
			uint32_t m_Requests = 0;
			LabParameterType m_Type = LabParameterType::Float;
			bool ReleaseOverride(const HostViewControlStamp& expected,
				const LabParameterId& parameter, LabParameterType type) noexcept override
			{
				++m_Requests;
				m_Type = type;
				m_Status = { .m_Parameter = parameter, .m_Expected = expected,
					.m_Batch = ParameterBatchStatus{ .m_BatchId = 17 } };
				return true;
			}
			const ViewOverrideReleaseStatus& GetReleaseStatus() const noexcept override { return m_Status; }
		};

		void RunViewControlPanelChecks(SelfTestContext& context) noexcept
		{
			ImGuiContext* previousContext = ImGui::GetCurrentContext();
			ImGui::CreateContext();
			auto& io = ImGui::GetIO();
			io.IniFilename = nullptr;
			io.LogFilename = nullptr;
			io.DisplaySize = ImVec2(1800, 1800);
			io.DeltaTime = 1.0f / 60.0f;
			unsigned char* pixels = nullptr;
			int width = 0, height = 0;
			io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);

			ApplicationViewSettingsResolution inherited;
			inherited.m_View.m_TemporalAA.m_Enabled = true;
			inherited.m_View.m_Lighting.m_GTAO.m_Radius = 2.0f;
			inherited.m_View.m_Exposure.m_ManualEV100 = 12.0f;
			inherited.m_ToolingBlocks.m_TemporalAA = true;
			const std::array pins
			{
				LabParameterValue{ LabParameterId("view.taa.enabled"), true },
				LabParameterValue{ LabParameterId("view.gtao.radius"), 1.5f },
				LabParameterValue{ LabParameterId("view.exposure.ev100"), 14.0f },
			};
			auto resolved = inherited.m_View;
			ApplyHostViewParameters(pins, resolved, true);
			FrameCaptureFrameState planned;
			planned.m_FrameSerial = 42;
			planned.m_HostViewControl = { std::string(32, 'a'), 2, 7 };
			planned.m_HostViewProjection = DescribeHostViewProjection(resolved, inherited.m_ToolingBlocks, pins);
			planned.m_Temporal.m_Consumers = {
				{ "temporal-aa", true, "unavailable", "core-capability-unavailable" },
				{ "temporal-gtao", false, "disabled", "not-requested" },
			};
			auto previous = planned;
			previous.m_FrameSerial = 41;
			previous.m_HostViewControl.m_ContentIdentity = 1;
			previous.m_Outcome = FrameCaptureFrameOutcome::Submitted;
			auto observation = BuildViewControlSnapshot(inherited, pins, planned, &previous);
			DevToolsRuntime devTools;
			DevelopGuiContext gui{
				.m_ViewRenderSettingsOverrides = &devTools.GetViewRenderSettingsOverrides(),
				.m_ViewControl = &observation,
			};
			TemporalAAInspectorPanel taa;
			GTAOInspectorPanel gtao;
			PostProcessInspectorPanel exposure;
			const auto draw = [&](DevelopGuiPanelBase& panel, const char* prefix)
				{
					ImGui::NewFrame();
					ImGui::SetNextWindowPos(ImVec2(0, 0));
					ImGui::SetNextWindowSize(ImVec2(1700, 1700));
					ImGui::Begin("ViewControlFixture");
					const ImGuiID prefixId = ImHashStr(prefix, 0, ImGui::GetCurrentWindow()->ID);
					ImGui::GetStateStorage()->SetBool(
						ImHashStr("Previous frame values and provenance", 0, prefixId), true);
					ImGui::LogToBuffer();
					panel.Draw(gui);
					const std::string text = ImGui::GetCurrentContext()->LogBuffer.c_str();
					ImGui::LogFinish();
					ImGui::End();
					ImGui::EndFrame();
					return text;
				};
			GGLAB_UNUSED(draw(taa, "view.taa."));
			const auto taaText = draw(taa, "view.taa.");
			context.Check(taaText.find("Masked by Host view overrides") != std::string::npos &&
				taaText.find("equal-value pins") != std::string::npos &&
				taaText.find("Host-resolved") != std::string::npos && taaText.find("tooling") != std::string::npos &&
				taaText.find("present") != std::string::npos && taaText.find("absent (inherit)") != std::string::npos,
				"TAA shows explicit equal pins, inherited tooling and Host-resolved values independently");
			context.Check(taaText.find("planned (not submitted)") != std::string::npos &&
				taaText.find("submitted (not GPU-complete evidence)") != std::string::npos &&
				taaText.find("Different run/content") != std::string::npos &&
				taaText.find("core-capability-unavailable") != std::string::npos &&
				taaText.find("pending GUI edits are not rendered observations") != std::string::npos,
				"GUI distinguishes frame outcomes, stale content, pending intent and unavailable temporal consumers");
			const auto gtaoText = draw(gtao, "view.gtao.");
			context.Check(gtaoText.find("radius") != std::string::npos &&
				gtaoText.find("1.5") != std::string::npos && gtaoText.find("content") != std::string::npos &&
				gtaoText.find("temporal-gtao: disabled") != std::string::npos,
				"GTAO exposes control provenance even without detailed renderer diagnostics");
			const auto exposureText = draw(exposure, "view.exposure.");
			context.Check(exposureText.find("ev100") != std::string::npos &&
				exposureText.find("compensation") != std::string::npos && exposureText.find("camera") != std::string::npos &&
				exposureText.find("14") != std::string::npos && exposureText.find("12") != std::string::npos,
				"Exposure observation separates camera inheritance from the Host override");
			for (auto& entry : observation.m_PlannedFrame.m_Values)
			{
				if (entry.m_Id == LabParameterId("view.taa.enabled"))
				{
					entry.m_Value = false;
					entry.m_Source = HostViewValueSource::ForcedReference;
				}
			}
			observation.m_PreviousFrame->m_Outcome = ViewControlFrameOutcome::Aborted;
			const auto masked = draw(taa, "view.taa.");
			context.Check(masked.find("Frame-local masking: forced-reference") != std::string::npos &&
				masked.find("aborted (no submitted-frame evidence)") != std::string::npos,
				"Reference masking and prior aborts remain visible rather than being presented as rendered Host intent");
			observation.m_PlannedFrame.m_Values.front().m_Source = HostViewValueSource::Sequence;
			const auto sequence = draw(taa, "view.taa.");
			context.Check(sequence.find("Frame-local masking: sequence") != std::string::npos,
				"Sequence masking is attributed to the frame-local layer rather than inferred from numeric equality");
			observation.m_PreviousFrame.reset();
			observation.m_PlannedFrame.m_Consumers.clear();
			const auto missing = draw(taa, "view.taa.");
			context.Check(missing.find("Previous frame observation unavailable") != std::string::npos &&
				missing.find("Temporal consumer observation unavailable") != std::string::npos,
				"Missing prior frame and consumer evidence remain explicitly unavailable");
			ReleaseControlProbe release;
			gui.m_ViewControlActions = &release;
			const auto activateRelease = [](const char* prefix, const char* parameter)
				{
					const auto window = ImHashStr("ViewControlFixture");
					const auto scope = ImHashStr(prefix, 0, window);
					const auto table = ImHashStr("ViewControlValues", 0, scope);
					const auto row = ImHashStr(parameter, 0, table);
					ImGui::ActivateItemByID(ImHashStr("Release override", 0, row));
				};
			activateRelease("view.taa.", "view.taa.enabled");
			GGLAB_UNUSED(draw(taa, "view.taa."));
			context.Check(release.m_Requests == 1 && release.m_Status.m_Expected == observation.m_Control &&
				release.m_Status.m_Parameter == LabParameterId("view.taa.enabled") && release.m_Type == LabParameterType::Bool &&
				std::get<bool>(*observation.m_HostView.front().m_Override) &&
				devTools.GetViewRenderSettingsOverrides().GetActiveCount() == 0,
				"The real Release button submits the displayed complete stamp and field type without editing Host/tooling observations");
			activateRelease("view.taa.", "view.taa.enabled");
			const auto pending = draw(taa, "view.taa.");
			context.Check(release.m_Requests == 1 && pending.find("pending") != std::string::npos,
				"Pending release disables repeated activation rather than adding a GUI FIFO");
			release.m_Status.m_Batch->m_State = ParameterBatchState::Finished;
			release.m_Status.m_Batch->m_HostViewOperation = HostViewOverrideOperation{
				.m_Expected = observation.m_Control, .m_After = observation.m_Control };
			release.m_Status.m_Batch->m_Items = { { .m_State = ParameterEffectState::NoChange } };
			const auto noChange = draw(taa, "view.taa.");
			context.Check(noChange.find("Control committed") != std::string::npos &&
				noChange.find("Effect: NoChange") != std::string::npos &&
				noChange.find("First consumed in submitted frame") == std::string::npos,
				"GUI separates a structural control commit from NoChange and never invents submitted evidence");
			release.m_Status.m_Batch.reset();
			release.m_Status.m_Error = "Busy: consume the previous parameter batch result first.";
			release.m_Status.m_BlockingOwner = ParameterBatchOwner{};
			const auto busy = draw(taa, "view.taa.");
			context.Check(busy.find("Busy:") != std::string::npos && busy.find("Blocking owner: external") != std::string::npos,
				"Release admission rejection shows Busy and the blocking owner");
			release.m_Status.m_Error.clear();
			release.m_Status.m_BlockingOwner.reset();
			activateRelease("view.gtao.", "view.gtao.radius");
			GGLAB_UNUSED(draw(gtao, "view.gtao."));
			context.Check(release.m_Requests == 2 && release.m_Status.m_Parameter == LabParameterId("view.gtao.radius") &&
				release.m_Type == LabParameterType::Float, "GTAO Release uses the same typed action capability");
			release.m_Status.m_Batch.reset();
			activateRelease("view.exposure.", "view.exposure.ev100");
			GGLAB_UNUSED(draw(exposure, "view.exposure."));
			context.Check(release.m_Requests == 3 && release.m_Status.m_Parameter == LabParameterId("view.exposure.ev100") &&
				release.m_Type == LabParameterType::Float, "Exposure Release targets the displayed view rather than authoring the camera");
			release.m_Status.m_Batch.reset();
			activateRelease("view.taa.", "view.taa.resolution_preset");
			GGLAB_UNUSED(draw(taa, "view.taa."));
			context.Check(release.m_Requests == 3, "An absent override has no active Release button");
			gui.m_ViewControlActions = nullptr;
			activateRelease("view.taa.", "view.taa.enabled");
			GGLAB_UNUSED(draw(taa, "view.taa."));
			context.Check(release.m_Requests == 3, "A missing action capability leaves observations read-only");
			gui.m_ViewControl = nullptr;
			const auto unavailable = draw(taa, "view.taa.");
			context.Check(unavailable.find("Display-view control observation unavailable") != std::string::npos &&
				unavailable.find("Host-resolved") == std::string::npos &&
				devTools.GetViewRenderSettingsOverrides().GetActiveCount() == 0 &&
				std::get<bool>(observation.m_HostView.front().m_Value),
				"Read-only panels neither invent absent observations nor mutate tooling or Host intent");
			ImGui::DestroyContext();
			ImGui::SetCurrentContext(previousContext);
		}

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
			snapshot.m_ForwardLighting = { ViewRenderFeatureState::Inactive, ViewRenderFeatureReason::NoOpaqueDraws };
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
				if (activation)
				{
					ImGui::ActivateItemByID(activation);
				}
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
				text.find("Forward+") != std::string::npos && text.find("No opaque draws") != std::string::npos &&
				text.find("Required capabilities unavailable") != std::string::npos && text.find("History: Valid") != std::string::npos,
				"Rendering Settings presents requested controls, runtime inactivity reasons and independent history");
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
			const std::array inspectIds =
			{
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
		gtaoSettings.m_Power = 2.0f;
		gtaoSettings.m_DirectionCount = 4;
		gtaoSettings.m_StepCount = 8;
		gtaoSettings.m_DenoiseRadius = 5;
		gtaoSettings.m_TemporalAccumulation = false;
		gtaoSettings.m_TemporalMaxSamples = 4;
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
		temporalSettings.m_VelocityWeightScale = 0.2f;
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
		expectedProfile = originalAuthoringProfile;
		expectedProfile.m_Lighting.m_GTAO = gtaoSettings;
		expectedProfile.m_TemporalAA = temporalSettings;
		expectedProfile.m_PostProcess.m_Bloom = bloomSettings;
		context.Check(overrides.GetActiveCount() == 4 &&
			ProfilesMatch(devTools.ResolveViewRenderProfile(authoringProfile), expectedProfile) &&
			ProfilesMatch(authoringProfile, originalAuthoringProfile),
			"All four overrides compose, counting blocks once and explicit false values as active");

		const auto frozenTooling = devTools.CaptureFrameSettings();
		overrides.m_GTAO.m_Settings.m_Radius = 9.0f;
		context.Check(frozenTooling.m_GTAO && frozenTooling.m_GTAO->m_Radius == gtaoSettings.m_Radius &&
			ProfilesMatch(frozenTooling.ResolveProfile(authoringProfile), expectedProfile),
			"A captured tooling snapshot owns its active blocks and explicit scalar values");
		overrides.m_GTAO.m_Settings = gtaoSettings;

		// A different authoring source must not silently clear session overrides or
		// re-seed the parameters of an already active settings block.
		ViewRenderProfile nextAuthoringProfile = originalAuthoringProfile;
		nextAuthoringProfile.m_Lighting.m_GTAO.m_Radius = 7.0f;
		nextAuthoringProfile.m_TemporalAA.m_MaxHistoryFeedback = 0.9f;
		nextAuthoringProfile.m_PostProcess.m_Bloom.m_Intensity = 0.4f;
		nextAuthoringProfile.m_EnableScenePreExposure = false;
		const ViewRenderProfile originalNextAuthoringProfile = nextAuthoringProfile;
		context.Check(overrides.GetActiveCount() == 4 &&
			ProfilesMatch(devTools.ResolveViewRenderProfile(nextAuthoringProfile), expectedProfile) &&
			ProfilesMatch(nextAuthoringProfile, originalNextAuthoringProfile),
			"DevTools session overrides survive authoring-profile switches without mutating either source");

		overrides.m_GTAO.Reset();
		expectedProfile.m_Lighting.m_GTAO = nextAuthoringProfile.m_Lighting.m_GTAO;
		context.Check(overrides.GetActiveCount() == 3 &&
			ProfilesMatch(devTools.ResolveViewRenderProfile(nextAuthoringProfile), expectedProfile),
			"Resetting one block restores the current authoring source and preserves sibling overrides");
		overrides.m_ScenePreExposure.reset();
		expectedProfile.m_EnableScenePreExposure = false;
		context.Check(overrides.GetActiveCount() == 2 &&
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
		RunViewControlPanelChecks(context);
		RunGIViewerPanelChecks(context);
		RunGIProbePickingGuiChecks(context);
	}
}
