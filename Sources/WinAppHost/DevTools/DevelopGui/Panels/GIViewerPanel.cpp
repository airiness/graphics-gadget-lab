#include "DevTools/DevelopGui/Panels/GIViewerPanel.h"
#include "DevTools/DevelopGui/DevelopGuiContext.h"
#include "DevTools/DevToolsRuntime.h"
#include "DevTools/DevelopGui/DevelopGuiTextureUtils.h"
#include "GGLabRuntime/Graphics/PostProcess/PostProcessPreviewControlBase.h"
#include "GGLabRuntime/Graphics/PostProcess/PostProcessPreviewViewBase.h"
#include "Capture/GIProbeInspectionControl.h"
#include "Capture/GIReceiverInspectionControl.h"
#include "Capture/GIProbePickingControl.h"
#include "GGLabFoundation/Base/CoreMacros.h"

#include <algorithm>
#include <array>
#include <imgui.h>

namespace gglab
{
	namespace
	{
		void DrawProbeArray(DevelopGuiContext& context) noexcept
		{
			if (!ImGui::CollapsingHeader("Probe array"))
			{
				return;
			}
			auto* overrides = context.m_ViewRenderSettingsOverrides;
			if (!overrides)
			{
				ImGui::TextDisabled("Probe display controls are unavailable.");
				return;
			}
			auto& settings = overrides->m_GIProbes;
			ImGui::Checkbox("Show probe spheres", &settings.m_Enabled);
			ImGui::SameLine();
			ImGui::Checkbox("Volume bounds", &settings.m_Bounds);
			int mode = int(settings.m_Mode);
			if (ImGui::Combo("Sphere display", &mode, "Classification\0Irradiance\0Change\0"))
			{
				settings.m_Mode = static_cast<GIProbeVisualizationMode>(mode);
			}
			ImGui::BeginDisabled(settings.m_Mode != GIProbeVisualizationMode::Irradiance);
			ImGui::SliderFloat("Sphere Preview EV", &settings.m_PreviewExposureEV,
				GIProbeVisualizationMinimumExposureEV, GIProbeVisualizationMaximumExposureEV, "%.2f");
			ImGui::EndDisabled();
			ImGui::BeginDisabled(settings.m_Mode != GIProbeVisualizationMode::Change);
			ImGui::Checkbox("Change / threshold", &settings.m_ChangeNormalized);
			ImGui::InputFloat("Change display maximum", &settings.m_ChangeRange);
			ImGui::Checkbox("Readiness block outlines", &settings.m_ChangeBlocks);
			ImGui::EndDisabled();
			int depthMode = int(settings.m_DepthMode);
			if (ImGui::Combo("Probe depth", &depthMode, "X-ray\0Scene depth\0"))
			{
				settings.m_DepthMode = static_cast<GIProbeDepthMode>(depthMode);
			}
			ImGui::SliderFloat("Radius / spacing", &settings.m_Radius, 0.01f, 0.5f, "%.3f");
			int budget = int(settings.m_Budget);
			if (ImGui::InputInt("Array budget", &budget))
			{
				settings.m_Budget = uint32_t(std::clamp(budget, 1, int(GIProbeVisualizationMaximumBudget)));
			}
			int axis = int(settings.m_SliceAxis);
			if (ImGui::Combo("Slice axis", &axis, "All\0X\0Y\0Z\0"))
			{
				settings.m_SliceAxis = uint32_t(axis);
			}
			int layers[2]{ int(settings.m_LayerMinimum), int(settings.m_LayerMaximum) };
			if (ImGui::InputInt2("Inclusive layers", layers))
			{
				settings.m_LayerMinimum = uint32_t(std::clamp(layers[0], 0, 65535));
				settings.m_LayerMaximum = uint32_t(std::clamp(layers[1], 0, 65535));
			}
			const std::array names{ "Pending", "Active", "Inactive", "Invalid" };
			for (uint32_t index = 0; index < names.size(); ++index)
			{
				if (index != 0)
				{
					ImGui::SameLine();
				}
				bool show = (settings.m_ClassificationMask & (1u << index)) != 0;
				if (ImGui::Checkbox(names[index], &show))
				{
					settings.m_ClassificationMask ^= 1u << index;
				}
			}
			ImGui::InputFloat("Maximum center distance (0 = off)", &settings.m_MaximumDistance);
			settings = ResolveGIProbeVisualizationSettings(settings);
			ImGui::TextDisabled("Native / TAA / Quality | read-only display depth | own depth resolves overlaps");
			const auto* summary = context.m_GIFieldSummary;
			const auto effectiveMode = summary ? summary->m_Visualization.m_Settings.m_Mode : settings.m_Mode;
			ImGui::TextDisabled(effectiveMode == GIProbeVisualizationMode::Irradiance ?
				"Active: white Lambertian E(n)/pi | independent EV, ACES + sRGB | gray silhouette" :
				"Active: solid green");
			ImGui::TextDisabled("Inactive: dim wire | Pending: gold rings | Invalid: magenta");
			ImGui::TextDisabled("Closing releases this GUI request. Host/session overrides keep precedence.");
			if (!summary)
			{
				ImGui::TextDisabled("No probe field in the active pipeline.");
				return;
			}
			const auto& display = summary->m_Visualization;
			if (display.m_Settings.m_Mode == GIProbeVisualizationMode::Change)
			{
				ImGui::TextWrapped("Completed relative change%s: blue through white to red; values above %.4g saturate. "
					"A low change can still be warming or waiting for the quiet hold.",
					display.m_Settings.m_ChangeNormalized ? " / tau" : "", display.m_Settings.m_ChangeRange);
				const auto& observation = display.m_ChangeObservation;
				if (!observation)
				{
					ImGui::TextDisabled(summary->m_Invalid ? "Change unavailable: field failed or completion unknown." :
						!summary->m_Readiness.m_Enabled ? "Change unavailable: readiness measurements disabled." :
						"Change pending: no completed measurement for this field/refresh epoch.");
				}
				else
				{
					const auto& data = *observation->m_Measurement;
					const auto& stamp = observation->m_Observation;
					ImGui::TextDisabled(data.m_IncludesDistance ?
						"Change includes irradiance and distance statistics; a black probe can still change." :
						"Lighting refresh: irradiance change only.");
					ImGui::TextDisabled("Block outlines: gray Excluded | gold warming | red p95/max | blue hold | green ready");
					ImGui::Text("Completed frame %llu | field update %llu | epoch %llu update %llu",
						static_cast<unsigned long long>(stamp.m_ProducingFrame), static_cast<unsigned long long>(stamp.m_FieldUpdate),
						static_cast<unsigned long long>(stamp.m_Field.m_RefreshEpoch), static_cast<unsigned long long>(stamp.m_ReadinessUpdate));
					ImGui::Text("%s | %s | alpha %.4g | noise %.4g | floor %.4g | tau %.4g / max %.4g",
						GetGIFieldReadinessName(data.m_Readiness.m_State).data(), data.m_RefreshMode ? "refresh" : "steady",
						data.m_Readiness.m_Hysteresis, data.m_Readiness.m_NoiseSigma, data.m_Readiness.m_IrradianceFloor,
						data.m_Readiness.m_Threshold, 3 * data.m_Readiness.m_Threshold);
					if (display.m_Settings.m_Selection &&
						MatchesGIProbeSelection(*display.m_Settings.m_Selection, stamp.m_Field.m_Generation, data.m_Counts))
					{
						const auto coordinate = display.m_Settings.m_Selection->m_Coordinate;
						const auto& probe = data.m_Probes[coordinate[0] + data.m_Counts[0] * (coordinate[2] + data.m_Counts[2] * coordinate[1])];
						ImGui::Text("Selected completed probe: %s | raw change %.6g | change/tau %.6g",
							probe.m_Active ? "Active" : "Inactive", probe.m_Change, probe.m_Change / data.m_Readiness.m_Threshold);
					}
					if (ImGui::BeginTable("Readiness blocks", 6, ImGuiTableFlags_Borders | ImGuiTableFlags_ScrollY, ImVec2(0, 180)))
					{
						for (const char* name : { "Block", "Active", "p95 / max", "Quiet hold", "Warming", "Blockers" })
						{
							ImGui::TableSetupColumn(name);
						}
						ImGui::TableHeadersRow();
						ImGuiListClipper clipper;
						clipper.Begin(int(data.m_Blocks.size()));
						while (clipper.Step())
						{
							for (int index = clipper.DisplayStart; index < clipper.DisplayEnd; ++index)
							{
								const auto& block = data.m_Blocks[index];
								ImGui::TableNextRow();
								ImGui::TableNextColumn();
								ImGui::Text("%d", index);
								ImGui::TableNextColumn();
								ImGui::Text("%u", block.m_ActiveProbes);
								ImGui::TableNextColumn();
								if (block.m_Percentile95)
								{
									ImGui::Text("%.4g / %.4g", *block.m_Percentile95, *block.m_Maximum);
								}
								else
								{
									ImGui::TextDisabled("Excluded");
								}
								ImGui::TableNextColumn();
								if (block.m_ActiveProbes != 0)
								{
									ImGui::Text("%u / %u", block.m_QuietUpdates, data.m_RequiredQuietUpdates);
									ImGui::TableNextColumn();
									ImGui::Text("%s (%u)", block.m_WarmingMet ? "met" : "pending", data.m_WarmingUpdates);
								}
								else
								{
									ImGui::TextDisabled("N/A");
									ImGui::TableNextColumn();
									ImGui::TextDisabled("Excluded");
								}
								ImGui::TableNextColumn();
								ImGui::Text("%s%s%s%s", (block.m_Blockers & 1) ? "warming " : "",
									(block.m_Blockers & 2) ? "p95 " : "", (block.m_Blockers & 4) ? "maximum " : "",
									(block.m_Blockers & 8) ? "hold" : "");
							}
						}
						ImGui::EndTable();
					}
				}
			}
			ImGui::Text("Effective request: %s | budget %u | generation %llu",
				display.m_Settings.m_Enabled ? "on" : "off", display.m_Settings.m_Budget,
				static_cast<unsigned long long>(summary->m_Field.m_Generation));
			ImGui::Text("Effective mode: %s | Preview EV %.2f",
				GetGIProbeVisualizationModeName(display.m_Settings.m_Mode).data(), display.m_Settings.m_PreviewExposureEV);
			ImGui::Text("Effective depth: %s", GetGIProbeDepthModeName(display.m_Settings.m_DepthMode).data());
			ImGui::TextDisabled("Depth alignment: %s", GetGIProbeVisualizationDepthAlignmentName(display.m_View).data());
			if (summary->m_ReceiverView.m_ReceiversAvailable)
			{
				ImGui::TextDisabled("Receiver diagnostic raster requires this recipe to use Native, TAA off.");
			}
			if (display.m_View.m_TemporalAA && !display.m_View.m_PostTemporalJittered)
			{
				ImGui::TextDisabled("Scene-depth silhouettes approximate the temporal image.");
			}
			if (display.m_Settings.m_Enabled && !display.m_Available)
			{
				ImGui::TextDisabled(!display.m_ViewSupported ? "Unsupported projection or display resolution." :
					"Probe display unavailable: pipeline, field or count readback could not prepare.");
			}
			if (display.m_CompletedObservation)
			{
				const auto& stamp = *display.m_CompletedObservation;
				const auto& counts = display.m_CompletedCounts;
				const bool historical = !MatchesGIProbeVisualizationCounts(*summary);
				ImGui::Text("%s counts at completed frame %llu: %u / %u displayed, %u budget drops",
					historical ? "Historical" : "Latest", static_cast<unsigned long long>(stamp.m_ProducingFrame),
					counts.m_Rendered, counts.m_Enumerated, counts.m_BudgetDropped);
				ImGui::Text("After slice / class / distance / frustum: %u / %u / %u / %u",
					counts.m_AfterSlice, counts.m_AfterClassification, counts.m_AfterDistance, counts.m_AfterFrustum);
				if (counts.m_BudgetDropped && display.m_Settings.m_DepthMode == GIProbeDepthMode::SceneDepth)
				{
					ImGui::TextWrapped("Scene Depth can hide the retained first-N probes. "
						"Increase the array budget, select layers, or use X-ray.");
				}
			}
			else
			{
				ImGui::TextDisabled("No completed display counts yet.");
			}
			ImGui::Text("Diagnostic highlights: %u | %s", display.m_HighlightCount,
				GetGIProbeHighlightStateName(display.m_HighlightState).data());
			ImGui::Text("Logical depth %llu B | selection %llu B | count buffers %llu B",
				static_cast<unsigned long long>(display.m_PrivateDepthLogicalBytes),
				static_cast<unsigned long long>(display.m_SelectionLogicalBytes),
				static_cast<unsigned long long>(display.m_ReadbackBufferBytes));
		}

		struct SignalOption
		{
			PostProcessDebugTap m_Tap;
			const char* m_Name;
		};
		constexpr std::array SignalOptions
		{
			SignalOption{ PostProcessDebugTap::GIIrradiance, "Irradiance" },
			SignalOption{ PostProcessDebugTap::GILambertRadiance, "Lambert Radiance" },
			SignalOption{ PostProcessDebugTap::GISunIrradiance, "Sun Irradiance" },
			SignalOption{ PostProcessDebugTap::GIEnvironmentIrradiance, "Environment Irradiance" },
			SignalOption{ PostProcessDebugTap::GIBaseVisibility, "Base Visibility" },
			SignalOption{ PostProcessDebugTap::GICoatVisibility, "Coat Visibility" },
			SignalOption{ PostProcessDebugTap::GICombinedVisibility, "Combined Visibility" },
		};
		const SignalOption& FindSignalOption(PostProcessDebugTap tap) noexcept
		{
			const auto found = std::ranges::find(SignalOptions, tap, &SignalOption::m_Tap);
			return found != SignalOptions.end() ? *found : SignalOptions.front();
		}

		void DrawProbeInspection(DevelopGuiContext& context, std::array<int, 3>& coordinate) noexcept
		{
			if (!ImGui::CollapsingHeader("DDGI probe inspection", ImGuiTreeNodeFlags_DefaultOpen))
			{
				return;
			}
			const auto* summary = context.m_GIFieldSummary;
			auto* control = context.m_GIProbeInspection;
			if (!summary || !control)
			{
				ImGui::TextDisabled("No DDGI field in the active pipeline.");
				return;
			}
			ImGui::Text("Field %llu | epoch %llu (U0=%llu) | %u x %u x %u",
				summary->m_Field.m_Generation, summary->m_Field.m_RefreshEpoch,
				summary->m_Field.m_EpochOriginUpdate, summary->m_Counts[0], summary->m_Counts[1], summary->m_Counts[2]);
			ImGui::Text("Readiness: %s | completed epoch update %llu | quiet blocks %u/%u",
				GetGIFieldReadinessName(summary->m_Readiness.m_State).data(),
				summary->m_Readiness.m_CompletedUpdates, summary->m_Readiness.m_QuietBlocks,
				summary->m_Readiness.m_ActiveBlocks);
			if (summary->m_CompletedMeasurement)
			{
				ImGui::Text("CPU measurement: frame %llu, field update %llu",
					summary->m_CompletedMeasurement->m_ProducingFrame,
					summary->m_CompletedMeasurement->m_FieldUpdate);
			}
			else
			{
				ImGui::TextDisabled("No completed readiness measurement in this epoch.");
			}
			if (ImGui::InputInt3("Probe coordinate (x,y,z)", coordinate.data()))
			{
				control->Cancel();
				if (context.m_ViewRenderSettingsOverrides)
				{
					context.m_ViewRenderSettingsOverrides->m_GIProbes.m_Selection.reset();
				}
			}
			GIProbeInspectionRequest request{
				.m_Coordinate = { uint32_t(coordinate[0]), uint32_t(coordinate[1]), uint32_t(coordinate[2]) },
				.m_ExpectedGeneration = summary->m_Field.m_Generation,
				.m_ExpectedRefreshEpoch = summary->m_Field.m_RefreshEpoch };
			const bool validSelection = MatchesGIProbeInspection(request, *summary);
			ImGui::BeginDisabled(!validSelection || !context.m_ViewRenderSettingsOverrides);
			if (ImGui::Button("Select / highlight probe"))
			{
				auto& display = context.m_ViewRenderSettingsOverrides->m_GIProbes;
				display.m_Enabled = true;
				display.m_Selection = GIProbeSelection{ request.m_Coordinate, request.m_ExpectedGeneration };
			}
			ImGui::EndDisabled();
			ImGui::TextDisabled("Effective highlight: %s | %u / %u diagnostic probes",
				GetGIProbeHighlightStateName(summary->m_Visualization.m_HighlightState).data(),
				summary->m_Visualization.m_HighlightCount, GIProbeVisualizationMaximumHighlights);
			if (summary->m_Visualization.m_Settings.m_Selection)
			{
				const auto& effective = *summary->m_Visualization.m_Settings.m_Selection;
				ImGui::Text("Effective selection: [%u,%u,%u] | generation %llu", effective.m_Coordinate[0],
					effective.m_Coordinate[1], effective.m_Coordinate[2], effective.m_ExpectedGeneration);
			}
			ImGui::BeginDisabled(control->IsPending() || !summary->m_InspectionAvailable ||
				!MatchesGIProbeInspection(request, *summary));
			if (ImGui::Button("Inspect probe"))
			{
				if (control->Inspect(request) && context.m_ViewRenderSettingsOverrides)
				{
					auto& display = context.m_ViewRenderSettingsOverrides->m_GIProbes;
					display.m_Enabled = true;
					display.m_Selection = GIProbeSelection{ request.m_Coordinate, request.m_ExpectedGeneration };
				}
			}
			ImGui::EndDisabled();
			ImGui::SameLine();
			if (ImGui::Button("Cancel / clear"))
			{
				control->Cancel();
				if (context.m_ViewRenderSettingsOverrides)
				{
					context.m_ViewRenderSettingsOverrides->m_GIProbes.m_Selection.reset();
				}
			}
			ImGui::TextWrapped("%s", control->GetStatus().c_str());
			const auto result = control->GetResult();
			if (!result)
			{
				return;
			}
			const bool historical = result->m_Observation.m_Field != summary->m_Field ||
				result->m_Request.m_Coordinate != request.m_Coordinate;
			ImGui::Text("%s | frame %llu | U=%llu, epoch update=%llu | classification %u | %s",
				historical ? "Historical field/selection" : "Completed GPU observation",
				result->m_Observation.m_ProducingFrame, result->m_Observation.m_FieldUpdate,
				result->m_Observation.m_ReadinessUpdate, result->m_Classification,
				GetGIProbeInspectionValidityName(result->m_Validity).data());
			ImGui::Text("World position: %.4f, %.4f, %.4f", result->m_WorldPosition[0],
				result->m_WorldPosition[1], result->m_WorldPosition[2]);
			ImGui::TextDisabled("Bordered octahedral texels | scene-referred E | no exposure or albedo");
			for (bool distance : { false, true })
			{
				if (!ImGui::TreeNode(distance ? "Distance: mean, second moment (world units, squared units)" :
					"Irradiance E: R, G, B"))
				{
					continue;
				}
				const auto& tile = distance ? result->m_Distance : result->m_Irradiance;
				const uint32_t width = distance ? result->m_DistanceTileWidth : result->m_IrradianceTileWidth;
				ImGui::BeginChild(distance ? "DistanceTexels" : "IrradianceTexels", ImVec2(0, 180));
				ImGuiListClipper clipper;
				clipper.Begin(static_cast<int>(tile.size()));
				while (clipper.Step())
				{
					for (int index = clipper.DisplayStart; index < clipper.DisplayEnd; ++index)
					{
						const auto& texel = tile[index];
						ImGui::Text("[%u,%u] %.7g  %.7g  %.7g", uint32_t(index) % width,
							uint32_t(index) / width, texel[0], texel[1], texel[2]);
					}
				}
				ImGui::EndChild();
				ImGui::TreePop();
			}
		}
		void DrawReceiverInspection(DevelopGuiContext& context, std::array<int, 2>& pixel,
			bool& markers, int& selectedCorner) noexcept
		{
			if (!ImGui::CollapsingHeader("Receiver contribution inspection"))
			{
				return;
			}
			const auto* summary = context.m_GIFieldSummary;
			auto* control = context.m_GIReceiverInspection;
			if (!summary || !control)
			{
				ImGui::TextDisabled("Receiver inspection is unavailable.");
				return;
			}
			const auto& view = summary->m_ReceiverView;
			ImGui::Text("View %u revision %llu | %u x %u | Native, unjittered, TAA off",
				view.m_ViewId, view.m_Revision, view.m_DisplayExtent[0], view.m_DisplayExtent[1]);
			ImGui::Text("Attachment logical sizes: receiver %.2f MiB | lookup %.2f MiB",
				double(view.m_ReceiverAttachmentBytes) / (1024 * 1024), double(view.m_LookupDiagnosticBytes) / (1024 * 1024));
			ImGui::TextDisabled("Query payload: 1,168 bytes | paired X-ray markers use 114 procedural vertices");
			if (!view.m_Supported)
			{
				ImGui::TextWrapped("Requires existing receiver attachments, Native rendering, no jitter and TAA off.");
			}
			else if (!summary->m_ReceiverInspectionAvailable)
			{
				ImGui::TextDisabled("Receiver inspection is unavailable for this frame.");
			}
			else if (markers && !summary->m_ReceiverMarkersAvailable)
			{
				ImGui::TextWrapped("X-ray markers are unavailable. Disable markers for numeric inspection.");
			}
			if (ImGui::InputInt2("Receiver pixel (top-left x,y)", pixel.data()))
			{
				control->Cancel();
			}
			if (ImGui::Button("Use view center"))
			{
				pixel = { int(view.m_DisplayExtent[0] / 2), int(view.m_DisplayExtent[1] / 2) };
				control->Cancel();
			}
			if (ImGui::Checkbox("Capture X-ray contribution markers", &markers))
			{
				control->Cancel();
			}
			GIReceiverInspectionRequest request{ .m_Pixel = { uint32_t(pixel[0]), uint32_t(pixel[1]) },
				.m_ExpectedGeneration = summary->m_Field.m_Generation,
				.m_ExpectedViewRevision = view.m_Revision, .m_ExpectedViewId = view.m_ViewId,
				.m_ExpectedRefreshEpoch = summary->m_Field.m_RefreshEpoch, .m_Markers = markers };
			ImGui::BeginDisabled(control->IsPending() || !summary->m_ReceiverInspectionAvailable ||
				(markers && !summary->m_ReceiverMarkersAvailable) ||
				!MatchesGIReceiverInspection(request, *summary));
			if (ImGui::Button("Inspect receiver"))
			{
				GGLAB_UNUSED(control->Inspect(request));
			}
			ImGui::EndDisabled();
			ImGui::SameLine();
			if (ImGui::Button("Clear receiver"))
			{
				control->Cancel();
			}
			ImGui::TextWrapped("%s", control->GetStatus().c_str());
			if (!control->GetImagePath().empty() && ImGui::Button("Copy paired image path"))
			{
				ImGui::SetClipboardText(control->GetImagePath().c_str());
			}
			const auto result = control->GetResult();
			if (!result)
			{
				return;
			}
			const bool historical = result->m_Observation.m_Field != summary->m_Field ||
				result->m_View.m_Revision != view.m_Revision || result->m_Request.m_Pixel != request.m_Pixel;
			ImGui::Text("%s | source frame %llu | U=%llu | view revision %llu",
				historical ? "Historical field/view/selection" : "Completed GPU observation",
				result->m_Observation.m_ProducingFrame, result->m_Observation.m_FieldUpdate, result->m_View.m_Revision);
			ImGui::Text("%s | coverage %.6g | total weight %.6g | support %u",
				GetGIReceiverInspectionValidityName(result->m_Validity).data(), result->m_Coverage,
				result->m_TotalWeight, result->m_Support);
			ImGui::Text("Receiver: %.4f, %.4f, %.4f | biased: %.4f, %.4f, %.4f",
				result->m_Position[0], result->m_Position[1], result->m_Position[2],
				result->m_BiasedPosition[0], result->m_BiasedPosition[1], result->m_BiasedPosition[2]);
			ImGui::Text("Scene-referred E: %.6g, %.6g, %.6g", result->m_IrradianceE[0],
				result->m_IrradianceE[1], result->m_IrradianceE[2]);
			ImGui::TextDisabled("Markers: cyan receiver, magenta bias, red inactive, blue/yellow normalized weight");
			if (!result->m_CageEvaluated)
			{
				ImGui::TextDisabled("Probe cage was not evaluated for this query outcome.");
				return;
			}
			if (result->m_TotalWeight == 0)
			{
				ImGui::TextDisabled("Normalization unavailable: no positive total weight.");
			}
			if (ImGui::BeginTable("ReceiverCorners", 9, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
				ImGuiTableFlags_ScrollX | ImGuiTableFlags_SizingFixedFit))
			{
				for (const char* name : { "Coordinate", "Reason", "Trilinear", "Backface", "Visibility cubed",
					"Before crush", "Raw weight", "Normalized", "Contribution E (RGB)" })
				{
					ImGui::TableSetupColumn(name);
				}
				ImGui::TableHeadersRow();
				for (size_t index = 0; index < result->m_Corners.size(); ++index)
				{
					const auto& corner = result->m_Corners[index];
					ImGui::TableNextRow();
					if (int(index) == selectedCorner)
					{
						ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, ImGui::GetColorU32(ImGuiCol_Header));
					}
					ImGui::TableNextColumn();
					ImGui::Text("%u,%u,%u", corner.m_Coordinate[0], corner.m_Coordinate[1], corner.m_Coordinate[2]);
					if (ImGui::IsItemClicked())
					{
						selectedCorner = int(index);
					}
					ImGui::TableNextColumn();
					ImGui::TextUnformatted(GetGIReceiverCornerReasonName(corner.m_Reason).data());
					ImGui::TableNextColumn();
					ImGui::Text("%.6g", corner.m_TrilinearWeight);
					for (float value : { corner.m_BackfaceFactor, corner.m_Visibility, corner.m_PreCrushWeight })
					{
						ImGui::TableNextColumn();
						if (corner.m_SamplingEvaluated == 1)
						{
							ImGui::Text("%.6g", value);
						}
						else
						{
							ImGui::TextUnformatted("Unavailable");
						}
					}
					ImGui::TableNextColumn();
					ImGui::Text("%.6g", corner.m_RawWeight);
					ImGui::TableNextColumn();
					if (corner.m_NormalizationAvailable == 1)
					{
						ImGui::Text("%.6g", corner.m_NormalizedWeight);
						ImGui::TableNextColumn();
						ImGui::Text("%.6g, %.6g, %.6g", corner.m_ContributionE[0], corner.m_ContributionE[1], corner.m_ContributionE[2]);
					}
					else
					{
						ImGui::TextUnformatted("Unavailable");
						ImGui::TableNextColumn();
						ImGui::TextUnformatted("Unavailable");
					}
				}
				ImGui::EndTable();
			}
			ImGui::TextDisabled("Select a coordinate for distance and atlas details.");
			if (selectedCorner >= 0 && size_t(selectedCorner) < result->m_Corners.size())
			{
				const auto& corner = result->m_Corners[selectedCorner];
				ImGui::Text("Probe position: %.5g, %.5g, %.5g | classification %u",
					corner.m_WorldPosition[0], corner.m_WorldPosition[1], corner.m_WorldPosition[2], corner.m_Classification);
				if (corner.m_SamplingEvaluated == 1)
				{
					ImGui::Text("Mean distance %.7g | receiver distance %.7g | variance %.7g | used variance %.7g",
						corner.m_MeanDistance, corner.m_ReceiverDistance, corner.m_Variance, corner.m_UsedVariance);
					ImGui::Text("Atlas M: %.7g, %.7g, %.7g | pre-visibility weight %.7g",
						corner.m_AtlasM[0], corner.m_AtlasM[1], corner.m_AtlasM[2], corner.m_PreVisibilityWeight);
				}
				else
				{
					ImGui::TextDisabled("Distance statistics and atlas sampling were not evaluated for this corner.");
				}
			}
		}
	}

	void GIViewerPanel::OnClosed(DevelopGuiContext& context) noexcept
	{
		m_PickingEnabled = false;
		m_PendingPick.reset();
		++m_PickToken;
		if (context.m_GIProbePicking)
		{
			context.m_GIProbePicking->Cancel();
		}
		if (context.m_ViewRenderSettingsOverrides)
		{
			context.m_ViewRenderSettingsOverrides->m_GIProbes.m_Enabled = false;
			context.m_ViewRenderSettingsOverrides->m_GIProbes.m_Selection.reset();
		}
		if (context.m_GIProbeInspection)
		{
			context.m_GIProbeInspection->Cancel();
		}
		if (context.m_GIReceiverInspection)
		{
			context.m_GIReceiverInspection->Cancel();
		}
	}

	void GIViewerPanel::OnFrame(DevelopGuiContext& context) noexcept
	{
		auto* control = context.m_GIProbePicking;
		auto* overrides = context.m_ViewRenderSettingsOverrides;
		if (!control || !overrides)
		{
			return;
		}
		const auto& io = ImGui::GetIO();
		const auto* viewport = ImGui::GetMainViewport();
		const GIProbePickMapping mapping{
			.m_GuiOrigin = { viewport->Pos.x, viewport->Pos.y },
			.m_GuiExtent = { io.DisplaySize.x, io.DisplaySize.y },
			.m_FramebufferScale = { io.DisplayFramebufferScale.x, io.DisplayFramebufferScale.y },
			.m_DpiScale = context.m_DisplayDpiScale };
		if (m_PendingPick)
		{
			const auto binding = context.m_GIFieldSummary ?
				CheckGIProbePickingBinding(*m_PendingPick, *context.m_GIFieldSummary, mapping) :
				GIProbePickingBinding::Unavailable;
			if (!m_PickingEnabled || context.m_ContentId != m_PickContent ||
				overrides->m_GIProbes != m_PickGuiSettings || binding != GIProbePickingBinding::Matching ||
				context.m_GIFieldSummary->m_Visualization.m_Settings.m_Selection != m_PickEffectiveSelection)
			{
				control->Cancel();
				m_PendingPick.reset();
				++m_PickToken;
				m_PickStatus = "GUI pick invalidated: " + std::string(GetGIProbePickingBindingName(binding));
			}
		}
		// Clicks bind to the last submitted display, never this planned GUI frame.
		if (m_PickingEnabled && context.m_AbsolutePointer && !io.WantCaptureMouse &&
			!ImGui::IsWindowHovered(ImGuiHoveredFlags_AnyWindow) && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
		{
			const auto& display = control->GetDisplay();
			const auto* summary = display.m_Summary.get();
			const auto pixel = summary ? MapGIProbePickPixel(mapping, summary->m_Visualization.m_View.m_DisplayExtent,
				{ io.MousePos.x, io.MousePos.y }) : std::nullopt;
			if (pixel && summary->m_Visualization.m_Observation && display.m_DpiScale == mapping.m_DpiScale)
			{
				const auto& source = summary->m_Visualization;
				GIProbePickingRequest request{
					.m_Token = ++m_PickToken,
					.m_SourceFrame = source.m_Observation->m_ProducingFrame,
					.m_SettingsRevision = source.m_SettingsRevision,
					.m_Field = summary->m_Field,
					.m_View = source.m_View,
					.m_Settings = GIProbePickSettings(source.m_Settings),
					.m_Mapping = mapping,
					.m_Pixel = *pixel };
				m_PendingPick.reset();
				if (control->Pick(request))
				{
					m_PendingPick = request;
					m_PickContent = display.m_ContentId;
					m_PickGuiSettings = overrides->m_GIProbes;
					m_PickEffectiveSelection = source.m_Settings.m_Selection;
				}
				else
				{
					control->Cancel();
				}
				m_PickStatus = control->GetStatus();
			}
			else
			{
				control->Cancel();
				m_PendingPick.reset();
				++m_PickToken;
				m_PickStatus = "Click has no matching displayed view / pixel mapping.";
			}
		}
		if (const auto result = control->GetResult(); m_PendingPick && result &&
			result->m_Request.m_Token == m_PickToken && result->m_Request == *m_PendingPick)
		{
			if (result->m_Validity == GIProbePickingValidity::Hit)
			{
				overrides->m_GIProbes.m_Selection = GIProbeSelection{
					.m_Coordinate = *result->m_Coordinate, .m_ExpectedGeneration = result->m_Request.m_Field.m_Generation };
				for (size_t axis = 0; axis < 3; ++axis)
				{
					m_ProbeCoordinate[axis] = int((*result->m_Coordinate)[axis]);
				}
			}
			else if (result->m_Validity == GIProbePickingValidity::NoHit)
			{
				overrides->m_GIProbes.m_Selection.reset();
			}
			m_PickStatus = control->GetStatus();
			m_PendingPick.reset();
		}
		else if (m_PendingPick)
		{
			m_PickStatus = control->GetStatus();
		}
	}

	void GIViewerPanel::Draw(DevelopGuiContext& context) noexcept
	{
		ImGui::SetWindowPos(ImVec2(460, 45), ImGuiCond_FirstUseEver);
		ImGui::SetWindowSize(ImVec2(760, 660), ImGuiCond_FirstUseEver);
		DrawProbeArray(context);
		ImGui::BeginDisabled(!context.m_GIProbePicking);
		ImGui::Checkbox("Pick probes with left click", &m_PickingEnabled);
		ImGui::EndDisabled();
		ImGui::TextWrapped("%s", m_PickStatus.c_str());
		ImGui::TextDisabled("Scene clicks | solid silhouette | async 1x1 | numeric inspection stays explicit");
		DrawProbeInspection(context, m_ProbeCoordinate);
		DrawReceiverInspection(context, m_ReceiverPixel, m_ReceiverMarkers, m_ReceiverCorner);
		const auto* view = context.m_PostProcessPreview;
		auto* control = context.m_PostProcessPreviewControl;
		if (!view || !control)
		{
			ImGui::TextDisabled("GI preview is unavailable.");
			return;
		}
		constexpr auto Channel = PostProcessPreviewChannel::GlobalIllumination;
		const auto preview = view->GetPostProcessPreviewDiagnostics(Channel);
		auto selection = preview.m_Selected;
		ImGui::BeginDisabled(m_Frozen);
		if (ImGui::BeginCombo("Signal", FindSignalOption(selection.m_Tap).m_Name))
		{
			for (const auto& option : SignalOptions)
			{
				const bool selected = option.m_Tap == selection.m_Tap;
				if (ImGui::Selectable(option.m_Name, selected))
				{
					selection.m_Tap = option.m_Tap;
					control->SetPostProcessPreviewSelection(selection, Channel);
				}
				if (selected)
				{
					ImGui::SetItemDefaultFocus();
				}
			}
			ImGui::EndCombo();
		}
		const bool visibility = IsGIVisibilityPreviewTap(selection.m_Tap);
		float exposure = preview.m_ExposureEV;
		ImGui::BeginDisabled(visibility);
		if (ImGui::SliderFloat("Preview EV", &exposure,
			GetPostProcessPreviewMinimumExposureEV(Channel), PostProcessPreviewMaximumExposureEV, "%.2f"))
		{
			control->SetPostProcessPreviewExposureEV(exposure, Channel);
		}
		ImGui::EndDisabled();
		ImGui::EndDisabled();
		ImGui::Checkbox("Freeze preview", &m_Frozen);
		ImGui::SameLine();
		ImGui::TextDisabled("Sampling continues");
		if (!m_Frozen)
		{
			control->RequestPostProcessPreview(Channel);
		}
		ImGui::TextDisabled(!visibility ? "Scene referred | ACES + sRGB | independent exposure" :
			"Visibility [0, 1] | black = occluded, white = visible");
		ImGui::TextDisabled("Magenta: invalid | checker: background/pending | blue: inactive lobe");

		const auto& source = preview.m_GISnapshot;
		const auto& publishedSignal = FindSignalOption(preview.m_Published.m_Tap);
		ImGui::Text("%s | generation %llu | samples %u / %u",
			m_Frozen ? "Frozen" : "Live", static_cast<unsigned long long>(source.m_Generation),
			source.m_Samples, source.m_TargetSamples);
		if (preview.m_HasPublished && IsGIDiffusePreviewTap(preview.m_Published.m_Tap))
		{
			ImGui::SameLine();
			ImGui::Text("| depth %u", source.m_MaximumDepth);
		}
		if (preview.m_HasPublished)
		{
			ImGui::TextDisabled("Last submitted %s: frame %llu | update %llu | %u x %u",
				publishedSignal.m_Name, static_cast<unsigned long long>(preview.m_FrameSerial),
				static_cast<unsigned long long>(preview.m_UpdateCount), preview.m_Width, preview.m_Height);
		}
		else
		{
			ImGui::TextDisabled("Waiting for a submitted preview.");
		}
		// The graph may already be recording the next live update. The provenance above
		// intentionally describes the last submitted preview, not a GPU-completion claim.
		ImGui::BeginChild("GIImage", ImVec2(0, 0), ImGuiChildFlags_Borders);
		if (preview.m_SourceAvailable && preview.m_HasPublished && preview.m_Published == selection &&
			preview.m_Width && preview.m_Height)
		{
			const ImTextureID texture = devtools::ResolveImGuiTextureId(
				context.m_DevelopGuiSystem, preview.m_SrvDescriptor);
			if (texture)
			{
				const ImVec2 available = ImGui::GetContentRegionAvail();
				const float aspect = static_cast<float>(preview.m_Width) / preview.m_Height;
				const float width = std::max(1.0f, std::min(available.x, available.y * aspect));
				ImGui::Image(texture, ImVec2(width, width / aspect));
			}
		}
		else
		{
			ImGui::TextWrapped("No preview yet. Open GI Reference in Lab Control and select a matching "
				"fixture. Visibility requires a specular-visibility fixture. Unfreeze to request an update.");
		}
		ImGui::EndChild();
	}
}
