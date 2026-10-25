#include "DevTools/DevelopGui/Panels/FrameCapturePanel.h"
#include "Application/Capture/ApplicationFrameCapture.h"
#include "DevTools/DevelopGui/DevelopGuiContext.h"
#include "GGLabRuntime/Diagnostics/DiagnosticsView.h"
#include "GGLabRuntime/Diagnostics/Snapshots/RenderGraphSnapshot.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <format>
#include <string>
#include <string_view>
#include <optional>
#include <utility>
#include <vector>

#include <imgui.h>

namespace gglab
{
	namespace
	{
		constexpr std::chrono::seconds NotificationDuration{ 5 };

		struct ResourceExportTarget
		{
			uint32_t m_Id = 0;
			RGResourceType m_Type = RGResourceType::RGTexture;
			RGResourceAddress m_Address;
		};

		struct FrameCapturePanelState
		{
			std::array<char, 512> m_OutputDirectory{};
			std::array<char, 128> m_Label{};
			std::array<char, 512> m_Note{};
			uint64_t m_OpenFolderJobId = 0;
			std::string m_OpenFolderFailure;
			std::vector<ResourceExportTarget> m_ResourceTargets;
			uint32_t m_NextTargetId = 1;
			uint32_t m_OpenTargetId = 0;
			ImGuiTextFilter m_ResourceFilter;
			bool m_IncludeGraph = false;
			bool m_GraphSummary = false;
			bool m_Initialized = false;
			std::optional<CaptureCancelResult> m_BatchCancellation;
		};

		template <size_t SIZE>
		void CopyToBuffer(std::array<char, SIZE>& buffer, std::string_view text) noexcept
		{
			const size_t length = std::min(text.size(), SIZE - 1);
			std::memcpy(buffer.data(), text.data(), length);
			buffer[length] = '\0';
		}

		[[nodiscard]] ImVec4 GetGateColor(FrameCaptureGateState state) noexcept
		{
			switch (state)
			{
			case FrameCaptureGateState::Ready:
				return ImVec4(0.45f, 0.85f, 0.45f, 1.0f);
			case FrameCaptureGateState::Failed:
				return ImVec4(0.95f, 0.40f, 0.40f, 1.0f);
			case FrameCaptureGateState::Pending:
				break;
			}
			return ImVec4(0.95f, 0.80f, 0.35f, 1.0f);
		}

		[[nodiscard]] std::string_view GetStatusName(CaptureJobStatus status) noexcept
		{
			switch (status)
			{
			case CaptureJobStatus::Completed:
				return "completed";
			case CaptureJobStatus::Cancelled:
				return "cancelled";
			case CaptureJobStatus::Failed:
				break;
			}
			return "failed";
		}

		void NextSetting(const char* label) noexcept
		{
			ImGui::TableNextRow();
			ImGui::TableSetColumnIndex(0);
			ImGui::AlignTextToFramePadding();
			ImGui::TextUnformatted(label);
			ImGui::TableSetColumnIndex(1);
			ImGui::SetNextItemWidth(-1.0f);
		}

		void SetupSettingsColumns() noexcept
		{
			ImGui::TableSetupColumn("##Label", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFontSize() * 8.5f);
			ImGui::TableSetupColumn("##Value", ImGuiTableColumnFlags_WidthStretch);
		}

		[[nodiscard]] const RGSnapshotResourceInfo* FindResource(
			const RGSnapshot* snapshot, std::string_view name) noexcept
		{
			if (snapshot)
			{
				for (const auto& resource : snapshot->m_Resources)
				{
					if (resource.m_Name == name)
					{
						return &resource;
					}
				}
			}
			return nullptr;
		}

		[[nodiscard]] bool PassMatches(const RGSnapshotPassInfo& pass,
			const RGResourceAddress& address) noexcept
		{
			return !pass.m_Observation && std::ranges::any_of(pass.m_Accesses,
				[&](const RGSnapshotAccessInfo& access)
				{
					return access.m_ResourceName == address.m_Resource &&
						(address.m_Position == RGObservationPosition::Before ?
							access.m_DependencyAccess != RGDependencyAccess::Write :
							access.m_WrittenResourceNodeIndex.has_value());
				});
		}

		[[nodiscard]] std::string AddressPositionText(const RGResourceAddress& address)
		{
			if (address.m_Position == RGObservationPosition::Final)
			{
				return "Final";
			}
			return std::format("{} {} [#{}]",
				address.m_Position == RGObservationPosition::Before ? "Before" : "After",
				address.m_Pass, address.m_Occurrence.value_or(1));
		}

		void DrawResourceChoices(FrameCapturePanelState& state, const RGSnapshot* snapshot) noexcept
		{
			std::vector<const RGSnapshotResourceInfo*> resources;
			if (snapshot)
			{
				for (const auto& resource : snapshot->m_Resources)
				{
					if (resource.m_ResourceExportSupport == RGObservationFailure::None)
					{
						resources.push_back(&resource);
					}
				}
			}
			std::ranges::sort(resources, {}, [](const auto* resource) { return std::string_view{ resource->m_Name }; });
			const std::string heading = std::format("Available resources ({})###ResourceChoices", resources.size());
			if (!ImGui::CollapsingHeader(heading.c_str(), ImGuiTreeNodeFlags_DefaultOpen))
			{
				return;
			}
			ImGui::SetNextItemWidth(-1.0f);
			if (ImGui::InputTextWithHint("##ResourceFilter", "Search resources", state.m_ResourceFilter.InputBuf,
				sizeof(state.m_ResourceFilter.InputBuf)))
			{
				state.m_ResourceFilter.Build();
			}
			if (resources.empty())
			{
				ImGui::TextDisabled("No exportable resources in the latest graph.");
				return;
			}
			if (ImGui::BeginTable("##AvailableResources", 3, ImGuiTableFlags_ScrollY |
				ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_SizingStretchProp,
				ImVec2(0.0f, ImGui::GetTextLineHeightWithSpacing() * 9.0f)))
			{
				ImGui::TableSetupColumn("Resource");
				ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFontSize() * 5.5f);
				ImGui::TableSetupColumn("##Add", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFontSize() * 2.5f);
				ImGui::TableSetupScrollFreeze(0, 1);
				ImGui::TableHeadersRow();
				for (const auto* resource : resources)
				{
					if (!state.m_ResourceFilter.PassFilter(resource->m_Name.c_str()))
					{
						continue;
					}
					ImGui::PushID(resource->m_Name.c_str());
					ImGui::TableNextRow();
					ImGui::TableNextColumn();
					ImGui::TextUnformatted(resource->m_Name.c_str());
					ImGui::SetItemTooltip("%s", resource->m_Name.c_str());
					ImGui::TableNextColumn();
					ImGui::TextUnformatted(resource->m_ResourceType == RGResourceType::RGTexture ? "Texture" : "Buffer");
					ImGui::TableNextColumn();
					if (ImGui::SmallButton("+"))
					{
						const uint32_t id = state.m_NextTargetId++;
						state.m_ResourceTargets.push_back({ id, resource->m_ResourceType,{ .m_Resource = resource->m_Name } });
						state.m_OpenTargetId = id;
					}
					ImGui::PopID();
				}
				ImGui::EndTable();
			}
		}

		void DrawTargetSettings(ResourceExportTarget& target, const RGSnapshot* snapshot) noexcept
		{
			auto& address = target.m_Address;
			if (ImGui::BeginTable("##TargetAddress", 2, ImGuiTableFlags_SizingStretchProp))
			{
				SetupSettingsColumns();
				NextSetting("At");
				int position = address.m_Position == RGObservationPosition::Final ?
					0 :
					address.m_Position == RGObservationPosition::Before ? 1 : 2;
				if (ImGui::Combo("##Position", &position, "Final\0Before pass\0After pass\0"))
				{
					address.m_Position = position == 0 ?
						RGObservationPosition::Final :
						position == 1 ? RGObservationPosition::Before : RGObservationPosition::After;
					address.m_Pass.clear();
					address.m_Occurrence.reset();
				}
				if (address.m_Position != RGObservationPosition::Final)
				{
					NextSetting("Pass");
					const std::string selectedPass = address.m_Pass.empty() ?
						"Select pass" :
						std::format("{} [#{}]", address.m_Pass, address.m_Occurrence.value_or(1));
					if (ImGui::BeginCombo("##Pass", selectedPass.c_str()))
					{
						if (snapshot)
						{
							for (const auto& pass : snapshot->m_Passes)
							{
								if (!PassMatches(pass, address))
								{
									continue;
								}
								const std::string label = std::format("{} [#{}]{}", pass.m_Name,
									pass.m_Occurrence, pass.m_Culled ? " (culled)" : "");
								if (ImGui::Selectable(label.c_str(), address.m_Pass == pass.m_Name &&
									address.m_Occurrence == pass.m_Occurrence))
								{
									address.m_Pass = pass.m_Name;
									address.m_Occurrence = pass.m_Occurrence;
								}
							}
						}
						ImGui::EndCombo();
					}
				}
				const auto* resource = FindResource(snapshot, address.m_Resource);
				if (target.m_Type == RGResourceType::RGTexture)
				{
					constexpr uint32_t zero = 0;
					constexpr uint32_t one = 1;
					const uint32_t maxMip = resource ? std::max<uint32_t>(resource->m_TextureMipLevels, 1) - 1 : 31;
					const uint32_t maxSlice = resource ? std::max<uint32_t>(resource->m_TextureArraySize, 1) - 1 : UINT32_MAX;
					NextSetting("Mip");
					ImGui::DragScalar("##Mip", ImGuiDataType_U32, &address.m_Mip, 1.0f, &zero, &maxMip);
					NextSetting("First slice");
					ImGui::DragScalar("##FirstSlice", ImGuiDataType_U32, &address.m_FirstSlice, 1.0f, &zero, &maxSlice);
					NextSetting("Slice count");
					bool allSlices = !address.m_SliceCount;
					if (ImGui::Checkbox("All remaining", &allSlices))
					{
						address.m_SliceCount = allSlices ? std::nullopt : std::optional{ 1u };
					}
					if (address.m_SliceCount)
					{
						ImGui::SameLine();
						ImGui::SetNextItemWidth(ImGui::GetFontSize() * 8.0f);
						ImGui::DragScalar("##SliceCount", ImGuiDataType_U32, &*address.m_SliceCount, 1.0f, &one);
					}
					NextSetting("Plane");
					int aspect = address.m_Aspect == RHITextureAspect::Color ?
						1 :
						address.m_Aspect == RHITextureAspect::Depth ?
							2 :
							address.m_Aspect == RHITextureAspect::Stencil ? 3 : 0;
					if (ImGui::Combo("##Plane", &aspect, "Automatic\0Color\0Depth\0Stencil\0"))
					{
						address.m_Aspect = aspect == 1 ?
							RHITextureAspect::Color :
							aspect == 2 ?
								RHITextureAspect::Depth :
								aspect == 3 ? RHITextureAspect::Stencil : RHITextureAspect::None;
					}
					ImGui::SetItemTooltip("Automatic selects color, or depth for a depth/stencil texture.");
				}
				ImGui::EndTable();
			}
			if (!FindResource(snapshot, address.m_Resource))
			{
				ImGui::TextDisabled("Not present in the latest graph.");
			}
		}

		void DrawResourceExport(ApplicationFrameCapture& frameCapture,
			FrameCapturePanelState& state, const RGSnapshot* snapshot) noexcept
		{
			DrawResourceChoices(state, snapshot);
			ImGui::Spacing();
			ImGui::SeparatorText(std::format("Export targets ({})", state.m_ResourceTargets.size()).c_str());
			std::optional<size_t> removeTarget;
			if (!state.m_ResourceTargets.empty() && ImGui::BeginTable("##ResourceTargets", 2,
				ImGuiTableFlags_ScrollY | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_SizingStretchProp,
				ImVec2(0.0f, ImGui::GetTextLineHeightWithSpacing() * 11.0f)))
			{
				ImGui::TableSetupColumn("##Target");
				ImGui::TableSetupColumn("##Remove", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFontSize() * 2.5f);
				for (size_t i = 0; i < state.m_ResourceTargets.size(); ++i)
				{
					auto& target = state.m_ResourceTargets[i];
					ImGui::PushID(static_cast<int>(target.m_Id));
					ImGui::TableNextRow();
					ImGui::TableNextColumn();
					if (state.m_OpenTargetId == target.m_Id)
					{
						ImGui::SetNextItemOpen(true);
						state.m_OpenTargetId = 0;
					}
					const bool expanded = ImGui::TreeNodeEx("##Target", ImGuiTreeNodeFlags_NoTreePushOnOpen |
						ImGuiTreeNodeFlags_SpanAvailWidth, "%s", target.m_Address.m_Resource.c_str());
					ImGui::TableNextColumn();
					if (ImGui::SmallButton("X"))
					{
						removeTarget = i;
					}
					ImGui::TableNextRow();
					ImGui::TableNextColumn();
					if (expanded)
					{
						ImGui::Indent();
						DrawTargetSettings(target, snapshot);
						ImGui::Unindent();
					}
					else
					{
						ImGui::TextWrapped("%s / mip %u", AddressPositionText(target.m_Address).c_str(), target.m_Address.m_Mip);
					}
					ImGui::PopID();
				}
				ImGui::EndTable();
			}
			if (removeTarget)
			{
				state.m_ResourceTargets.erase(state.m_ResourceTargets.begin() + *removeTarget);
			}
			const bool configured = std::ranges::all_of(state.m_ResourceTargets, [](const ResourceExportTarget& target)
				{
					const auto& address = target.m_Address;
					return (address.m_Position == RGObservationPosition::Final || !address.m_Pass.empty()) &&
						(!address.m_SliceCount || *address.m_SliceCount > 0);
				});
			ImGui::Spacing();
			ImGui::BeginDisabled(state.m_ResourceTargets.empty() || !configured);
			if (ImGui::Button("Export Resources"))
			{
				std::vector<RGResourceAddress> addresses;
				for (const auto& target : state.m_ResourceTargets)
				{
					addresses.push_back(target.m_Address);
				}
				frameCapture.ExportResources(std::move(addresses), state.m_IncludeGraph);
			}
			ImGui::EndDisabled();
			if (!configured && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
			{
				ImGui::SetTooltip("Select a pass and a positive slice count for every target.");
			}
			ImGui::SameLine();
			ImGui::Checkbox("Include graph", &state.m_IncludeGraph);
		}

		void DrawSettings(ApplicationFrameCapture& frameCapture, FrameCapturePanelState& state,
			DiagnosticsView* diagnostics) noexcept
		{
			ApplicationFrameCapture::Settings& settings = frameCapture.GetSettings();
			if (!state.m_Initialized)
			{
				CopyToBuffer(state.m_OutputDirectory, ApplicationFrameCapture::GetPathText(settings.m_OutputDirectory));
				CopyToBuffer(state.m_Label, settings.m_Label);
				CopyToBuffer(state.m_Note, settings.m_Note);
				state.m_Initialized = true;
			}
			const FrameCaptureFrameState* frameState = frameCapture.GetLastFrameState();
			if (ImGui::BeginTable("##CaptureSettings", 2, ImGuiTableFlags_SizingStretchProp))
			{
				SetupSettingsColumns();
				NextSetting("Timing");
				int timing = static_cast<int>(settings.m_Timing);
				if (ImGui::Combo("##Timing", &timing, "Next frame\0After ready\0"))
				{
					settings.m_Timing = static_cast<FrameCaptureTiming>(timing);
				}
				if (settings.m_Timing == FrameCaptureTiming::AfterReady)
				{
					constexpr uint32_t minimum = 0;
					constexpr uint32_t maximum = 10000;
					NextSetting("Settle frames");
					ImGui::DragScalar("##SettleFrames", ImGuiDataType_U32, &settings.m_SettleFrames, 1.0f, &minimum, &maximum);
				}
				NextSetting("Reference view");
				if (ImGui::BeginCombo("##ReferenceView", settings.m_ReferenceViewId.empty() ?
					"Current camera" :
					settings.m_ReferenceViewId.c_str()))
				{
					if (ImGui::Selectable("Current camera", settings.m_ReferenceViewId.empty()))
					{
						settings.m_ReferenceViewId.clear();
					}
					if (frameState)
					{
						for (const std::string& viewId : frameState->m_ReferenceViewIds)
						{
							if (ImGui::Selectable(viewId.c_str(), settings.m_ReferenceViewId == viewId))
							{
								settings.m_ReferenceViewId = viewId;
							}
						}
					}
					ImGui::EndCombo();
				}
				NextSetting("Output directory");
				const std::string defaultDirectory = ApplicationFrameCapture::GetPathText(frameCapture.GetDefaultOutputDirectory());
				if (ImGui::InputTextWithHint("##OutputDirectory", defaultDirectory.c_str(),
					state.m_OutputDirectory.data(), state.m_OutputDirectory.size()))
				{
					const char* text = state.m_OutputDirectory.data();
					settings.m_OutputDirectory = std::filesystem::path(std::u8string(text, text + std::strlen(text)));
				}
				NextSetting("Label");
				if (ImGui::InputText("##Label", state.m_Label.data(), state.m_Label.size()))
				{
					settings.m_Label = state.m_Label.data();
				}
				NextSetting("Note");
				if (ImGui::InputTextWithHint("##Note", "Optional", state.m_Note.data(), state.m_Note.size()))
				{
					settings.m_Note = state.m_Note.data();
				}
				ImGui::EndTable();
			}
			ImGui::Spacing();
			if (ImGui::BeginTabBar("##CaptureActions"))
			{
				if (ImGui::BeginTabItem("Screenshot"))
				{
					ImGui::SetNextItemWidth(180.0f);
					int source = static_cast<int>(settings.m_Source);
					if (ImGui::Combo("Source", &source, "Scene\0Composited\0"))
					{
						settings.m_Source = static_cast<CaptureImageSource>(source);
					}
					ImGui::SetItemTooltip("Scene excludes tooling overlays; Composited includes them.");
					if (ImGui::Button("Capture (F9)"))
					{
						frameCapture.Capture();
					}
					ImGui::SameLine();
					const size_t viewCount = frameState ? frameState->m_ReferenceViewIds.size() : 0;
					const auto& batch = frameCapture.GetReferenceViewBatch();
					ImGui::BeginDisabled(viewCount == 0 ||
						batch.m_State == ApplicationFrameCapture::ReferenceViewBatchState::Running || batch.m_JobId != 0);
					if (ImGui::Button(std::format("Capture All Views ({})", viewCount).c_str()))
					{
						frameCapture.CaptureReferenceViews();
						state.m_BatchCancellation.reset();
					}
					ImGui::EndDisabled();
					ImGui::EndTabItem();
				}
				if (ImGui::BeginTabItem("Render Graph"))
				{
					if (ImGui::Button("Export Graph"))
					{
						frameCapture.ExportGraph(state.m_GraphSummary);
					}
					ImGui::SameLine();
					ImGui::Checkbox("Summary only", &state.m_GraphSummary);
					ImGui::EndTabItem();
				}
				if (ImGui::BeginTabItem("Resources"))
				{
					const RGSnapshot* snapshot = diagnostics ? diagnostics->GetSnapshot<RGSnapshot>() : nullptr;
					DrawResourceExport(frameCapture, state, snapshot);
					ImGui::EndTabItem();
				}
				ImGui::EndTabBar();
			}
			ImGui::Spacing();
			ImGui::TextDisabled("%u pending jobs", frameCapture.GetPendingJobCount());
			const CaptureAdmission admission = frameCapture.GetLastAdmission();
			if (admission.m_Status != CaptureAdmissionStatus::Accepted)
			{
				ImGui::TextColored(GetGateColor(FrameCaptureGateState::Pending), "Admission %s: %s",
					admission.m_Status == CaptureAdmissionStatus::Busy ? "busy" : "rejected",
					ApplicationFrameCapture::GetAdmissionReasonName(admission.m_Reason).data());
			}
			const auto& batch = frameCapture.GetReferenceViewBatch();
			if (batch.m_State != ApplicationFrameCapture::ReferenceViewBatchState::Idle)
			{
				const char* status = batch.m_State == ApplicationFrameCapture::ReferenceViewBatchState::Running ? "running" :
					batch.m_State == ApplicationFrameCapture::ReferenceViewBatchState::Completed ? "completed" :
					batch.m_State == ApplicationFrameCapture::ReferenceViewBatchState::Cancelled ? "cancelled" : "failed";
				ImGui::Text("Reference views: %s, %u/%u published (%u admitted)", status,
					batch.m_CompletedViews, batch.m_TotalViews, batch.m_AcceptedViews);
				if (batch.m_State == ApplicationFrameCapture::ReferenceViewBatchState::Running && ImGui::Button("Cancel views"))
				{
					state.m_BatchCancellation = frameCapture.CancelReferenceViews();
				}
				if (state.m_BatchCancellation)
				{
					ImGui::TextDisabled("Cancellation: %s",
						ApplicationFrameCapture::GetCancellationName(*state.m_BatchCancellation).data());
				}
			}
		}

		void DrawFrameState(const ApplicationFrameCapture& frameCapture) noexcept
		{
			const FrameCaptureFrameState* frameState = frameCapture.GetLastFrameState();
			if (!frameState)
			{
				ImGui::TextDisabled("No frame has been rendered yet.");
				return;
			}

			ImGui::Text("Backend: %s", frameState->m_Backend.c_str());
			ImGui::Text("Demo: %s", frameState->m_DemoId.c_str());
			ImGui::Text("Lab: %s",
				frameState->m_LabId.empty() ? "-" : frameState->m_LabId.c_str());
			ImGui::Text("Camera: %s (%.2f, %.2f, %.2f), %.1f deg",
				frameState->m_Camera.m_Name.c_str(), frameState->m_Camera.m_Position[0],
				frameState->m_Camera.m_Position[1], frameState->m_Camera.m_Position[2],
				frameState->m_Camera.m_VerticalFovDegrees);
			const std::string timeStep = frameState->m_FixedDeltaTime ?
				std::format("fixed {:.4f} s", *frameState->m_FixedDeltaTime) :
				std::string("wall clock");
			ImGui::Text("Time step: %s", timeStep.c_str());

			const bool ready = frameState->m_Readiness.IsReady();
			ImGui::TextColored(GetGateColor(ready ?
				FrameCaptureGateState::Ready :
				frameState->m_Readiness.HasFailed() ?
					FrameCaptureGateState::Failed :
					FrameCaptureGateState::Pending),
				"Readiness: %s, %u settled frames", ready ? "ready" : "not ready",
				frameCapture.GetSettledFrameCount());

			constexpr ImGuiTableFlags flags = ImGuiTableFlags_Borders |
				ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp;
			if (ImGui::BeginTable("##FrameCaptureGates", 3, flags))
			{
				ImGui::TableSetupColumn("Gate");
				ImGui::TableSetupColumn("State");
				ImGui::TableSetupColumn("Detail");
				ImGui::TableHeadersRow();
				for (const FrameCaptureGate& gate : frameState->m_Readiness.m_Gates)
				{
					ImGui::TableNextRow();
					ImGui::TableNextColumn();
					ImGui::TextUnformatted(gate.m_Name.c_str());
					ImGui::TableNextColumn();
					ImGui::TextColored(GetGateColor(gate.m_State), "%s",
						GetFrameCaptureGateStateName(gate.m_State).data());
					ImGui::TableNextColumn();
					ImGui::TextUnformatted(gate.m_Detail.c_str());
				}
				ImGui::EndTable();
			}
		}

		void DrawOutputPath(const std::filesystem::path& path) noexcept
		{
			if (path.empty())
			{
				return;
			}
			const std::string text = ApplicationFrameCapture::GetPathText(path);
			ImGui::PushID(text.c_str());
			if (ImGui::Selectable(ApplicationFrameCapture::GetPathText(path.filename()).c_str(), false,
				ImGuiSelectableFlags_None, ImVec2(ImGui::GetContentRegionAvail().x, 0.0f)))
			{
				ImGui::SetClipboardText(text.c_str());
			}
			ImGui::SetItemTooltip("Copy path:\n%s", text.c_str());
			ImGui::PopID();
		}

		void DrawHistory(const ApplicationFrameCapture& frameCapture, FrameCapturePanelState& state) noexcept
		{
			const auto& history = frameCapture.GetHistory();
			if (history.empty())
			{
				ImGui::TextDisabled("No finished captures.");
			}
			for (auto entry = history.rbegin(); entry != history.rend(); ++entry)
			{
				const CaptureJobResult& result = entry->m_Result;
				ImGui::PushID(std::to_string(result.m_JobId).c_str());
				const bool completed = ApplicationFrameCapture::IsPublished(result);
				const auto* metadata = ApplicationFrameCapture::GetMetadata(result);
				const std::string frame = metadata ? std::format(" / frame {}", metadata->m_FrameSerial) : "";
				const ImGuiTreeNodeFlags flags = entry == history.rbegin() ? ImGuiTreeNodeFlags_DefaultOpen : 0;
				const bool open = ImGui::TreeNodeEx("##Result", flags, "Job #%llu / %s / %s%s",
					static_cast<unsigned long long>(result.m_JobId), GetStatusName(result.m_Status).data(),
					ApplicationFrameCapture::GetPublicationName(result.m_Publication).data(), frame.c_str());
				if (open)
				{
					DrawOutputPath(result.m_ManifestPath);
					for (const auto& product : result.m_Products)
					{
						const char* kind = product.m_Kind == CaptureProductKind::Image ? "Image" :
							product.m_Kind == CaptureProductKind::Diagnostic ? "Diagnostic" :
							product.m_Kind == CaptureProductKind::Resource ? "Resource" : "Graph";
						ImGui::Text("%u: %s / %s / %s", product.m_ProductIndex, kind,
							GetStatusName(product.m_Status).data(),
							ApplicationFrameCapture::GetPublicationName(product.m_Publication).data());
						for (const auto& path : product.m_Files)
						{
							DrawOutputPath(path);
						}
						if (product.m_ResourceFailure != RGObservationFailure::None)
						{
							ImGui::TextDisabled("%s", GetRGObservationFailureName(product.m_ResourceFailure).data());
						}
						if (!product.m_Failure.empty())
						{
							ImGui::TextWrapped("%s", product.m_Failure.c_str());
						}
					}
					ImGui::BeginDisabled(!completed || !frameCapture.CanOpenOutputFolder(result));
					if (ImGui::Button("Open folder"))
					{
						state.m_OpenFolderJobId = result.m_JobId;
						frameCapture.OpenOutputFolder(result.m_JobId, state.m_OpenFolderFailure);
					}
					ImGui::EndDisabled();
					if (state.m_OpenFolderJobId == result.m_JobId && !state.m_OpenFolderFailure.empty())
					{
						ImGui::TextWrapped("%s", state.m_OpenFolderFailure.c_str());
					}
					ImGui::TreePop();
				}
				ImGui::PopID();
			}
		}

	}

	void FrameCapturePanel::Draw(DevelopGuiContext& context) noexcept
	{
		ImGui::SetWindowSize(ImVec2(720.0f, 700.0f), ImGuiCond_FirstUseEver);
		if (!m_FrameCapture)
		{
			ImGui::TextDisabled("Frame capture is not available.");
			return;
		}

		auto& state = context.PanelState<FrameCapturePanelState>();
		if (ImGui::CollapsingHeader("Capture", ImGuiTreeNodeFlags_DefaultOpen))
		{
			DrawSettings(*m_FrameCapture, state, context.m_Diagnostics);
		}
		if (ImGui::CollapsingHeader("Current Frame"))
		{
			DrawFrameState(*m_FrameCapture);
		}
		if (ImGui::CollapsingHeader("History", ImGuiTreeNodeFlags_DefaultOpen))
		{
			DrawHistory(*m_FrameCapture, state);
		}
	}

	void DrawFrameCaptureNotification(const ApplicationFrameCapture& frameCapture) noexcept
	{
		const auto& history = frameCapture.GetHistory();
		const ImGuiViewport* viewport = ImGui::GetMainViewport();
		const CaptureAdmission admission = frameCapture.GetLastAdmission();
		const bool showAdmission = admission.m_Status != CaptureAdmissionStatus::Accepted &&
			(history.empty() || frameCapture.GetLastAdmissionTime() > history.back().m_FinishedAt);
		if (!showAdmission && history.empty())
		{
			return;
		}
		const auto timestamp = showAdmission ? frameCapture.GetLastAdmissionTime() :
			history.back().m_FinishedAt;
		if (!viewport || std::chrono::steady_clock::now() - timestamp > NotificationDuration)
		{
			return;
		}

		const ImVec2 position(viewport->WorkPos.x + viewport->WorkSize.x - 16.0f,
			viewport->WorkPos.y + viewport->WorkSize.y - 16.0f);
		ImGui::SetNextWindowPos(position, ImGuiCond_Always, ImVec2(1.0f, 1.0f));
		ImGui::SetNextWindowBgAlpha(0.85f);
		constexpr ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration |
			ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoMove |
			ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoInputs |
			ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoFocusOnAppearing;
		if (ImGui::Begin("##FrameCaptureNotification", nullptr, flags))
		{
			ImGui::PushTextWrapPos(ImGui::GetFontSize() * 40.0f);
			if (showAdmission)
			{
				ImGui::Text("Capture admission %s: %s",
					admission.m_Status == CaptureAdmissionStatus::Busy ? "busy" : "rejected",
					ApplicationFrameCapture::GetAdmissionReasonName(admission.m_Reason).data());
			}
			else
			{
				const CaptureJobResult& result = history.back().m_Result;
				const bool completed = ApplicationFrameCapture::IsPublished(result);
				ImGui::TextColored(GetGateColor(completed ? FrameCaptureGateState::Ready : FrameCaptureGateState::Failed),
					"Job #%llu %s / %s", static_cast<unsigned long long>(result.m_JobId),
					GetStatusName(result.m_Status).data(),
					ApplicationFrameCapture::GetPublicationName(result.m_Publication).data());
				ImGui::TextUnformatted(completed ?
					ApplicationFrameCapture::GetPathText(ApplicationFrameCapture::GetOutputPath(result)).c_str() :
					std::string(ApplicationFrameCapture::GetFailure(result)).c_str());
			}
			ImGui::PopTextWrapPos();
		}
		ImGui::End();
	}
}
