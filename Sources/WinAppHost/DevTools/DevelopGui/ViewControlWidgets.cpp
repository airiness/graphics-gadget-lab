#include "DevTools/DevelopGui/ViewControlWidgets.h"
#include "DevTools/EnumText/EnumTextGraphics.h"
#include "Parameters/ViewControlSnapshot.h"
#include "Parameters/ViewControlActions.h"
#include "GGLabFoundation/Base/CoreMacros.h"

#include <algorithm>
#include <format>
#include <string>
#include <type_traits>
#include <variant>

#include <imgui.h>

namespace gglab::devtools
{
	namespace
	{
		constexpr ImVec4 MaskColor(0.95f, 0.75f, 0.25f, 1.0f);

		[[nodiscard]] std::string ValueText(const LabValue& value, const LabParameterDesc* desc)
		{
			if (desc && desc->m_Type == LabParameterType::Enum)
			{
				if (const auto* number = std::get_if<int32_t>(&value))
				{
					const auto item = std::ranges::find(desc->m_EnumItems, *number, &LabEnumItem::m_Value);
					if (item != desc->m_EnumItems.end())
					{
						return item->m_Name;
					}
				}
			}
			return std::visit([](const auto& item) -> std::string
				{
					using T = std::remove_cvref_t<decltype(item)>;
					if constexpr (std::is_same_v<T, bool>)
					{
						return item ? "true" : "false";
					}
					else if constexpr (std::is_same_v<T, float>)
					{
						return std::format("{:.6g}", item);
					}
					else if constexpr (std::is_integral_v<T>)
					{
						return std::to_string(item);
					}
					else
					{
						return "Unavailable";
					}
				}, value);
		}

		void DrawValue(const LabValue& value, HostViewValueSource source,
			const LabParameterDesc* desc) noexcept
		{
			ImGui::TextUnformatted(ValueText(value, desc).c_str());
			const auto name = GetHostViewValueSourceName(source);
			ImGui::TextDisabled("%.*s", static_cast<int>(name.size()), name.data());
		}

		[[nodiscard]] const char* OutcomeName(ViewControlFrameOutcome outcome) noexcept
		{
			switch (outcome)
			{
			case ViewControlFrameOutcome::Planned:
				return "planned (not submitted)";
			case ViewControlFrameOutcome::Submitted:
				return "submitted (not GPU-complete evidence)";
			case ViewControlFrameOutcome::Aborted:
				return "aborted (no submitted-frame evidence)";
			}
			return "unavailable";
		}

		void DrawFrameHeading(const char* label, const ViewControlFrameObservation& frame) noexcept
		{
			ImGui::TextWrapped("%s: %llu | %s | %s", label,
				static_cast<unsigned long long>(frame.m_FrameSerial), OutcomeName(frame.m_Outcome),
				EnumText(frame.m_DisplayViewId).c_str());
		}

		void DrawConsumers(const ViewControlFrameObservation& frame, std::string_view prefix) noexcept
		{
			const std::string_view name = prefix == "view.taa." ?
				"temporal-aa" :
				prefix == "view.gtao." ? "temporal-gtao" : "";
			if (name.empty())
			{
				return;
			}
			const auto consumer = std::ranges::find(frame.m_Consumers, name,
				&FrameCaptureTemporalConsumer::m_Name);
			if (consumer == frame.m_Consumers.end())
			{
				ImGui::TextDisabled("Temporal consumer observation unavailable.");
				return;
			}
			ImGui::TextWrapped("%s: %s | requested: %s | reason: %s",
				consumer->m_Name.c_str(), consumer->m_Status.c_str(),
				consumer->m_Requested ? "true" : "false", consumer->m_DisableReason.c_str());
		}

		[[nodiscard]] const char* EffectName(ParameterEffectState state) noexcept
		{
			switch (state)
			{
			case ParameterEffectState::Requested:
				return "Requested";
			case ParameterEffectState::Applied:
				return "Applied";
			case ParameterEffectState::Published:
				return "Published";
			case ParameterEffectState::Consumed:
				return "Consumed";
			case ParameterEffectState::Superseded:
				return "Superseded";
			case ParameterEffectState::NoChange:
				return "NoChange";
			case ParameterEffectState::EffectNotReached:
				return "EffectNotReached";
			case ParameterEffectState::Failed:
				return "Failed";
			}
			return "Unavailable";
		}

		void DrawReleaseStatus(const ViewControlActionsBase* actions, std::string_view prefix) noexcept
		{
			if (!actions)
			{
				return;
			}
			const auto& status = actions->GetReleaseStatus();
			const auto id = status.m_Parameter.GetName();
			if (!id.starts_with(prefix))
			{
				return;
			}
			ImGui::TextWrapped("Release expected: run %s | content %llu | Host revision %llu",
				status.m_Expected.m_RunIdentity.c_str(),
				static_cast<unsigned long long>(status.m_Expected.m_ContentIdentity),
				static_cast<unsigned long long>(status.m_Expected.m_HostViewRevision));
			if (!status.m_Error.empty())
			{
				ImGui::TextWrapped("Release %.*s rejected: %s", static_cast<int>(id.size()), id.data(),
					status.m_Error.c_str());
				if (status.m_BlockingOwner)
				{
					const auto owner = *status.m_BlockingOwner;
					const char* kind = owner.IsExternal() ?
						"external" :
						owner.GetKind() == ParameterBatchOwnerKind::Sequence ? "sequence" : "tooling";
					ImGui::Text("Blocking owner: %s (%llu)", kind,
						static_cast<unsigned long long>(owner.GetIdentity()));
				}
				return;
			}
			if (const auto& batch = status.m_Batch)
			{
				const char* state = status.IsPending() ?
					"pending" :
					batch->m_State == ParameterBatchState::Failed ? "failed" : "finished";
				ImGui::Text("Release %.*s | batch %llu | %s", static_cast<int>(id.size()), id.data(),
					static_cast<unsigned long long>(batch->m_BatchId), state);
				if (batch->m_HostViewOperation && batch->m_HostViewOperation->m_After)
				{
					ImGui::Text("Control committed | Host revision: %llu",
						static_cast<unsigned long long>(batch->m_HostViewOperation->m_After->m_HostViewRevision));
				}
				for (const auto& item : batch->m_Items)
				{
					ImGui::Text("Effect: %s", EffectName(item.m_State));
					if (item.m_FirstConsumed)
					{
						ImGui::Text("First consumed in submitted frame: %llu",
							static_cast<unsigned long long>(*item.m_FirstConsumed));
					}
				}
				if (!batch->m_Failure.empty())
				{
					ImGui::TextWrapped("%s", batch->m_Failure.c_str());
				}
			}
		}

		[[nodiscard]] const HostViewProjectionEntry* FindValue(
			const ViewControlFrameObservation& frame, const LabParameterId& id) noexcept
		{
			const auto found = std::ranges::find(frame.m_Values, id, &HostViewProjectionEntry::m_Id);
			return found == frame.m_Values.end() ? nullptr : &*found;
		}
	}

	void DrawViewControlSnapshot(const ViewControlSnapshot* snapshot,
		ViewControlActionsBase* actions, std::string_view parameterPrefix) noexcept
	{
		ImGui::PushID(parameterPrefix.data(), parameterPrefix.data() + parameterPrefix.size());
		if (!snapshot)
		{
			ImGui::TextDisabled("Display-view control observation unavailable.");
			ImGui::PopID();
			return;
		}
		const bool hostMasked = std::ranges::any_of(snapshot->m_HostView, [parameterPrefix](const auto& entry)
			{
				return entry.m_Id.GetName().starts_with(parameterPrefix) && entry.m_Override.has_value();
			});
		bool sequenceMasked = false;
		bool referenceMasked = false;
		for (const auto& entry : snapshot->m_PlannedFrame.m_Values)
		{
			if (!entry.m_Id.GetName().starts_with(parameterPrefix))
			{
				continue;
			}
			sequenceMasked |= entry.m_Source == HostViewValueSource::Sequence;
			referenceMasked |= entry.m_Source == HostViewValueSource::ForcedReference;
		}
		if (hostMasked)
		{
			ImGui::TextColored(MaskColor, "Masked by Host view overrides (including equal-value pins).");
		}
		if (sequenceMasked || referenceMasked)
		{
			ImGui::TextColored(MaskColor, "Frame-local masking: %s",
				referenceMasked ? "forced-reference" : "sequence");
		}
		if (ImGui::CollapsingHeader("Display-view control", ImGuiTreeNodeFlags_DefaultOpen))
		{
			ImGui::TextWrapped("Run: %s | Content: %llu | Host revision: %llu",
				snapshot->m_Control.m_RunIdentity.c_str(),
				static_cast<unsigned long long>(snapshot->m_Control.m_ContentIdentity),
				static_cast<unsigned long long>(snapshot->m_Control.m_HostViewRevision));
			ImGui::TextWrapped("GUI edits remain in the tooling/camera layer. They enter a later tick; "
				"pending GUI edits are not rendered observations.");
			DrawReleaseStatus(actions, parameterPrefix);
			DrawFrameHeading("Current frame", snapshot->m_PlannedFrame);
			DrawConsumers(snapshot->m_PlannedFrame, parameterPrefix);
			if (ImGui::BeginTable("ViewControlValues", 5,
				ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable))
			{
				ImGui::TableSetupColumn("Parameter");
				ImGui::TableSetupColumn("Inherited");
				ImGui::TableSetupColumn("Host override");
				ImGui::TableSetupColumn("Host-resolved");
				ImGui::TableSetupColumn("Planned frame");
				ImGui::TableHeadersRow();
				for (const auto& entry : snapshot->m_HostView)
				{
					const std::string_view id = entry.m_Id.GetName();
					if (!id.starts_with(parameterPrefix))
					{
						continue;
					}
					const auto* inherited = snapshot->m_Inherited.Find(entry.m_Id);
					const auto* desc = inherited ? &inherited->m_Desc : nullptr;
					ImGui::TableNextRow();
					ImGui::TableSetColumnIndex(0);
					const auto label = id.substr(parameterPrefix.size());
					ImGui::TextWrapped("%.*s", static_cast<int>(label.size()), label.data());
					ImGui::TableSetColumnIndex(1);
					if (inherited)
					{
						DrawValue(inherited->m_Value, entry.m_InheritedSource, desc);
					}
					else
					{
						ImGui::TextDisabled("Unavailable");
					}
					ImGui::TableSetColumnIndex(2);
					if (entry.m_Override)
					{
						ImGui::TextUnformatted(ValueText(*entry.m_Override, desc).c_str());
						ImGui::TextDisabled("present");
						ImGui::PushID(id.data(), id.data() + id.size());
						ImGui::BeginDisabled(!actions || !desc || snapshot->m_Control.m_RunIdentity.empty() ||
							(actions && actions->GetReleaseStatus().IsPending()));
						if (ImGui::SmallButton("Release override"))
						{
							GGLAB_UNUSED(actions->ReleaseOverride(snapshot->m_Control, entry.m_Id, desc->m_Type));
						}
						ImGui::EndDisabled();
						ImGui::PopID();
					}
					else
					{
						ImGui::TextDisabled("absent (inherit)");
					}
					ImGui::TableSetColumnIndex(3);
					DrawValue(entry.m_Value, entry.m_Source, desc);
					ImGui::TableSetColumnIndex(4);
					if (const auto* planned = FindValue(snapshot->m_PlannedFrame, entry.m_Id))
					{
						DrawValue(planned->m_Value, planned->m_Source, desc);
					}
					else
					{
						ImGui::TextDisabled("Unavailable");
					}
				}
				ImGui::EndTable();
			}
			if (!snapshot->m_PreviousFrame)
			{
				ImGui::TextDisabled("Previous frame observation unavailable.");
			}
			else
			{
				const auto& previous = *snapshot->m_PreviousFrame;
				DrawFrameHeading("Previous frame", previous);
				if (ImGui::TreeNode("Previous frame values and provenance"))
				{
					ImGui::TextWrapped("Run: %s | Content: %llu | Host revision: %llu",
						previous.m_Control.m_RunIdentity.c_str(),
						static_cast<unsigned long long>(previous.m_Control.m_ContentIdentity),
						static_cast<unsigned long long>(previous.m_Control.m_HostViewRevision));
					if (previous.m_Control.m_RunIdentity != snapshot->m_Control.m_RunIdentity ||
						previous.m_Control.m_ContentIdentity != snapshot->m_Control.m_ContentIdentity)
					{
						ImGui::TextColored(MaskColor, "Different run/content: not evidence of current state.");
					}
					DrawConsumers(previous, parameterPrefix);
					for (const auto& entry : previous.m_Values)
					{
						if (entry.m_Id.GetName().starts_with(parameterPrefix))
						{
							const auto* parameter = snapshot->m_Inherited.Find(entry.m_Id);
							ImGui::Text("%.*s: %s | %s", static_cast<int>(entry.m_Id.GetName().size()),
								entry.m_Id.GetName().data(),
								ValueText(entry.m_Value, parameter ? &parameter->m_Desc : nullptr).c_str(),
								GetHostViewValueSourceName(entry.m_Source).data());
						}
					}
					ImGui::TreePop();
				}
			}
		}
		ImGui::PopID();
	}
}
