#include "DevTools/DevelopGui/Panels/GIViewerPanel.h"
#include "DevTools/DevelopGui/DevelopGuiContext.h"
#include "DevTools/DevToolsRuntime.h"
#include "DevTools/DevelopGui/DevelopGuiTextureUtils.h"
#include "GGLabRuntime/Graphics/PostProcess/PostProcessPreviewControlBase.h"
#include "GGLabRuntime/Graphics/PostProcess/PostProcessPreviewViewBase.h"
#include "Capture/GIProbeInspectionControl.h"
#include "Capture/GIReceiverInspectionControl.h"
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
			if (ImGui::Combo("Sphere display", &mode, "Classification\0Irradiance\0"))
			{
				settings.m_Mode = static_cast<GIProbeVisualizationMode>(mode);
			}
			ImGui::BeginDisabled(settings.m_Mode != GIProbeVisualizationMode::Irradiance);
			ImGui::SliderFloat("Sphere Preview EV", &settings.m_PreviewExposureEV,
				GIProbeVisualizationMinimumExposureEV, GIProbeVisualizationMaximumExposureEV, "%.2f");
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
			ImGui::TextDisabled("Native, TAA off | scene depth is read-only | own depth resolves sphere overlaps");
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
			ImGui::Text("Effective request: %s | budget %u | generation %llu",
				display.m_Settings.m_Enabled ? "on" : "off", display.m_Settings.m_Budget,
				static_cast<unsigned long long>(summary->m_Field.m_Generation));
			ImGui::Text("Effective mode: %s | Preview EV %.2f",
				GetGIProbeVisualizationModeName(display.m_Settings.m_Mode).data(), display.m_Settings.m_PreviewExposureEV);
			ImGui::Text("Effective depth: %s", GetGIProbeDepthModeName(display.m_Settings.m_DepthMode).data());
			if (display.m_Settings.m_Enabled && !display.m_Available)
			{
				ImGui::TextDisabled(!display.m_ViewSupported ? "Unsupported view: use Native with TAA off." :
					"Probe display unavailable: pipeline, field or count readback could not prepare.");
			}
			if (display.m_CompletedObservation)
			{
				const auto& stamp = *display.m_CompletedObservation;
				const auto& counts = display.m_CompletedCounts;
				const bool historical = stamp.m_Field != summary->m_Field ||
					display.m_CompletedSettings != display.m_Settings ||
					display.m_CompletedView.m_Revision != summary->m_ReceiverView.m_Revision;
				ImGui::Text("%s counts at completed frame %llu: %u / %u displayed, %u budget drops",
					historical ? "Historical" : "Latest", static_cast<unsigned long long>(stamp.m_ProducingFrame),
					counts.m_Rendered, counts.m_Enumerated, counts.m_BudgetDropped);
				ImGui::Text("After slice / class / distance / frustum: %u / %u / %u / %u",
					counts.m_AfterSlice, counts.m_AfterClassification, counts.m_AfterDistance, counts.m_AfterFrustum);
			}
			else
			{
				ImGui::TextDisabled("No completed display counts yet.");
			}
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
			}
			GIProbeInspectionRequest request{
				.m_Coordinate = { uint32_t(coordinate[0]), uint32_t(coordinate[1]), uint32_t(coordinate[2]) },
				.m_ExpectedGeneration = summary->m_Field.m_Generation,
				.m_ExpectedRefreshEpoch = summary->m_Field.m_RefreshEpoch };
			ImGui::BeginDisabled(control->IsPending() || !summary->m_InspectionAvailable ||
				!MatchesGIProbeInspection(request, *summary));
			if (ImGui::Button("Inspect probe"))
			{
				GGLAB_UNUSED(control->Inspect(request));
			}
			ImGui::EndDisabled();
			ImGui::SameLine();
			if (ImGui::Button("Cancel / clear"))
			{
				control->Cancel();
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
		if (context.m_ViewRenderSettingsOverrides)
		{
			context.m_ViewRenderSettingsOverrides->m_GIProbes.m_Enabled = false;
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

	void GIViewerPanel::Draw(DevelopGuiContext& context) noexcept
	{
		ImGui::SetWindowPos(ImVec2(460, 45), ImGuiCond_FirstUseEver);
		ImGui::SetWindowSize(ImVec2(760, 660), ImGuiCond_FirstUseEver);
		DrawProbeArray(context);
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
