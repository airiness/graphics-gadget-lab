#include "Core/Precompiled.h"
#include "DevTools/DevelopGui/Panels/LabPanel.h"
#include "Application/Lab/LabInterfaces.h"
#include "Application/Lab/LabRuntime.h"
#include "Diagnostics/DiagnosticsRuntime.h"
#include "DevTools/EnumText/EnumTextGraphics.h"
#include "DevTools/DevelopGui/DevelopGuiContext.h"

namespace gglab
{
	namespace
	{
		const LabId CullingLabId("gglab.lab.culling");

		const char* ToText(LabRunState state) noexcept
		{
			switch (state)
			{
			case LabRunState::Uninitialized:
				return "Uninitialized";
			case LabRunState::Loading:
				return "Loading";
			case LabRunState::WarmingUp:
				return "Warming Up";
			case LabRunState::Ready:
				return "Ready";
			case LabRunState::Capturing:
				return "Capturing";
			case LabRunState::Completed:
				return "Completed";
			case LabRunState::Failed:
				return "Failed";
			}
			return "Unknown";
		}

		template<typename T>
		const T* GetOptionalValue(const std::optional<LabValue>& value) noexcept
		{
			return value ? std::get_if<T>(&*value) : nullptr;
		}

		void DrawCullingLabStatistics(const DevelopGuiContext& context) noexcept
		{
			if (context.m_RenderQueues.empty())
			{
				ImGui::TextDisabled("Render queue statistics are not available yet.");
				return;
			}

			constexpr ImGuiTableFlags tableFlags =
				ImGuiTableFlags_Borders |
				ImGuiTableFlags_RowBg |
				ImGuiTableFlags_Resizable |
				ImGuiTableFlags_SizingStretchProp;
			if (!ImGui::BeginTable("CullingLabRenderQueues", 6, tableFlags))
			{
				return;
			}
			ImGui::TableSetupColumn("RenderView", ImGuiTableColumnFlags_WidthStretch);
			ImGui::TableSetupColumn("Total", ImGuiTableColumnFlags_WidthFixed, 64.0f);
			ImGui::TableSetupColumn("Visible", ImGuiTableColumnFlags_WidthFixed, 64.0f);
			ImGui::TableSetupColumn("Culled", ImGuiTableColumnFlags_WidthFixed, 64.0f);
			ImGui::TableSetupColumn("Invalid", ImGuiTableColumnFlags_WidthFixed, 64.0f);
			ImGui::TableSetupColumn("Draw Items", ImGuiTableColumnFlags_WidthFixed, 80.0f);
			ImGui::TableHeadersRow();

			for (const RenderQueue& queue : context.m_RenderQueues)
			{
				if (queue.m_ViewId == RenderViewID::Unknown)
				{
					continue;
				}
				const RenderQueueStatistics& stats = queue.m_Statistics;
				if (stats.m_TotalInstanceCount == 0 &&
					stats.m_VisibleInstanceCount == 0 &&
					stats.m_CulledInstanceCount == 0 &&
					stats.m_InvalidInstanceCount == 0)
				{
					continue;
				}

				ImGui::TableNextRow();
				ImGui::TableSetColumnIndex(0);
				ImGui::TextUnformatted(devtools::EnumText(queue.m_ViewId).c_str());
				ImGui::TableSetColumnIndex(1);
				ImGui::Text("%u", stats.m_TotalInstanceCount);
				ImGui::TableSetColumnIndex(2);
				ImGui::Text("%u", stats.m_VisibleInstanceCount);
				ImGui::TableSetColumnIndex(3);
				ImGui::Text("%u", stats.m_CulledInstanceCount);
				ImGui::TableSetColumnIndex(4);
				ImGui::Text("%u", stats.m_InvalidInstanceCount);
				ImGui::TableSetColumnIndex(5);
				ImGui::Text("%u", stats.m_DrawItemCount);
			}

			ImGui::EndTable();
		}
	}

	void LabPanel::Draw(DevelopGuiContext& context) noexcept
	{
		if (!m_RuntimeLocator)
		{
			ImGui::TextDisabled("Lab runtime is not available.");
			return;
		}
		LabRuntime* runtime = m_RuntimeLocator->GetLabRuntimeIfCreated();
		if (!runtime)
		{
			ImGui::TextDisabled("Demo.LabHost has not been created yet.");
			return;
		}

		LabSnapshot sourceSnapshot{};
		const LabSnapshot* snapshotPtr = context.m_Diagnostics ?
			context.m_Diagnostics->GetSnapshot<LabSnapshot>() : nullptr;
		if (!snapshotPtr)
		{
			sourceSnapshot = runtime->GetLabSnapshot();
			snapshotPtr = &sourceSnapshot;
		}
		const LabSnapshot& snapshot = *snapshotPtr;
		bool commandQueued = false;

		ImGui::TextUnformatted("Active Lab");
		ImGui::SetNextItemWidth(-FLT_MIN);
		const char* activeName = snapshot.m_ActiveLabName.empty() ?
			"None" : snapshot.m_ActiveLabName.c_str();
		if (ImGui::BeginCombo("##ActiveLab", activeName))
		{
			for (const LabDescriptor& descriptor : snapshot.m_AvailableLabs)
			{
				const bool selected = descriptor.m_Id == snapshot.m_ActiveLabId;
				ImGui::PushID(descriptor.m_Id.m_Name.c_str());
				if (ImGui::Selectable(descriptor.m_DisplayName.c_str(), selected))
				{
					runtime->RequestSwitchLab(descriptor.m_Id);
					commandQueued = true;
				}
				if (selected)
				{
					ImGui::SetItemDefaultFocus();
				}
				ImGui::PopID();
			}
			ImGui::EndCombo();
		}

		ImGui::Text("State: %s", ToText(snapshot.m_State));
		ImGui::Text("Session frame: %llu", snapshot.m_FrameInSession);
		if (snapshot.m_State == LabRunState::WarmingUp)
		{
			ImGui::Text("Warm-up remaining: %u", snapshot.m_WarmupFramesRemaining);
		}
		ImGui::Text("Effective delta: %.6f s", snapshot.m_EffectiveDeltaTime);
		if (snapshot.m_LastFrame.m_HasFeedback)
		{
			ImGui::Text(
				"Last submitted frame: %llu (back buffer %u, fence %llu)",
				snapshot.m_LastFrame.m_ApplicationFrameIndex,
				snapshot.m_LastFrame.m_BackBufferIndex,
				snapshot.m_LastFrame.m_SubmittedFenceValue);
		}
		if (!snapshot.m_IsHostActive)
		{
			ImGui::TextDisabled("Demo.LabHost is inactive; commands will apply when it becomes active.");
		}
		if (!snapshot.m_Description.empty())
		{
			ImGui::TextWrapped("%s", snapshot.m_Description.c_str());
		}

		if (snapshot.m_ActiveLabId == CullingLabId)
		{
			ImGui::SeparatorText("Culling Statistics");
			DrawCullingLabStatistics(context);
		}

		if (ImGui::Button("Reset Parameters"))
		{
			runtime->RequestResetParameters();
			commandQueued = true;
		}
		ImGui::SameLine();
		if (ImGui::Button("Rebuild Scene"))
		{
			runtime->RequestRebuildScene();
			commandQueued = true;
		}
		ImGui::SameLine();
		if (ImGui::Button("Restart Session"))
		{
			runtime->RequestRestartSession();
			commandQueued = true;
		}

		if (!m_RunConfigDraft || m_RunConfigLabId != snapshot.m_ActiveLabId)
		{
			m_RunConfigDraft = snapshot.m_RunConfig;
			m_RunConfigLabId = snapshot.m_ActiveLabId;
		}
		if (m_RunConfigDraft && ImGui::CollapsingHeader("Run Configuration"))
		{
			LabRunConfig& config = *m_RunConfigDraft;
			ImGui::InputScalar(
				"Random Seed",
				ImGuiDataType_U64,
				&config.m_RandomSeed);
			ImGui::DragScalar(
				"Warm-up Frames",
				ImGuiDataType_U32,
				&config.m_WarmupFrames,
				1.0f);
			ImGui::Checkbox("Use Fixed Delta Time", &config.m_UseFixedDeltaTime);
			ImGui::BeginDisabled(!config.m_UseFixedDeltaTime);
			ImGui::DragFloat(
				"Fixed Delta Time",
				&config.m_FixedDeltaTime,
				0.0001f,
				1.0f / 1000.0f,
				1.0f,
				"%.6f s");
			ImGui::EndDisabled();
			if (ImGui::Button("Restart with Run Config"))
			{
				config.Sanitize();
				runtime->RequestRunConfig(config);
				commandQueued = true;
			}
		}

		std::string_view currentGroup;
		for (const LabParameterSnapshot& parameter : snapshot.m_Parameters)
		{
			if (parameter.m_Desc.m_Group != currentGroup)
			{
				currentGroup = parameter.m_Desc.m_Group;
				ImGui::SeparatorText(currentGroup.empty() ? "Parameters" : currentGroup.data());
			}

			ImGui::PushID(parameter.m_Desc.m_Id.m_Name.c_str());
			if (DrawParameter(parameter))
			{
				commandQueued = true;
			}
			ImGui::PopID();
		}

		if (snapshot.m_HasPendingCommands || commandQueued)
		{
			ImGui::TextDisabled("Pending changes will apply at the next LabHost frame boundary.");
		}
		if (!snapshot.m_LastError.empty())
		{
			ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.3f, 1.0f), "%s", snapshot.m_LastError.c_str());
		}
	}

	bool LabPanel::DrawParameter(const LabParameterSnapshot& parameter) noexcept
	{
		const LabParameterDesc& desc = parameter.m_Desc;
		LabValue value = parameter.m_Value;
		bool changed = false;

		switch (desc.m_Type)
		{
		case LabParameterType::Bool:
		{
			bool current = std::get<bool>(value);
			changed = ImGui::Checkbox(desc.m_Name.c_str(), &current);
			value = current;
			break;
		}
		case LabParameterType::Int:
		{
			int32_t current = std::get<int32_t>(value);
			const int32_t* minValue = GetOptionalValue<int32_t>(desc.m_MinValue);
			const int32_t* maxValue = GetOptionalValue<int32_t>(desc.m_MaxValue);
			changed = ImGui::DragInt(
				desc.m_Name.c_str(),
				&current,
				1.0f,
				minValue ? *minValue : 0,
				maxValue ? *maxValue : 0);
			value = current;
			break;
		}
		case LabParameterType::UInt:
		{
			uint32_t current = std::get<uint32_t>(value);
			const uint32_t* minValue = GetOptionalValue<uint32_t>(desc.m_MinValue);
			const uint32_t* maxValue = GetOptionalValue<uint32_t>(desc.m_MaxValue);
			changed = ImGui::DragScalar(
				desc.m_Name.c_str(),
				ImGuiDataType_U32,
				&current,
				1.0f,
				minValue,
				maxValue);
			value = current;
			break;
		}
		case LabParameterType::Float:
		{
			float current = std::get<float>(value);
			const float* minValue = GetOptionalValue<float>(desc.m_MinValue);
			const float* maxValue = GetOptionalValue<float>(desc.m_MaxValue);
			changed = ImGui::DragFloat(
				desc.m_Name.c_str(),
				&current,
				0.05f,
				minValue ? *minValue : 0.0f,
				maxValue ? *maxValue : 0.0f);
			value = current;
			break;
		}
		case LabParameterType::Enum:
		{
			int32_t current = std::get<int32_t>(value);
			const auto selected = std::ranges::find_if(desc.m_EnumItems, [current](const LabEnumItem& item)
				{
					return item.m_Value == current;
				});
			const char* preview = selected != desc.m_EnumItems.end() ? selected->m_Name.c_str() : "Unknown";
			if (ImGui::BeginCombo(desc.m_Name.c_str(), preview))
			{
				for (const LabEnumItem& item : desc.m_EnumItems)
				{
					const bool isSelected = item.m_Value == current;
					if (ImGui::Selectable(item.m_Name.c_str(), isSelected))
					{
						current = item.m_Value;
						changed = true;
					}
					if (isSelected)
					{
						ImGui::SetItemDefaultFocus();
					}
				}
				ImGui::EndCombo();
			}
			value = current;
			break;
		}
		case LabParameterType::Vector3:
		{
			Vector3 current = std::get<Vector3>(value);
			const Vector3* minValue = GetOptionalValue<Vector3>(desc.m_MinValue);
			const Vector3* maxValue = GetOptionalValue<Vector3>(desc.m_MaxValue);
			changed = ImGui::DragFloat3(
				desc.m_Name.c_str(),
				&current.m_X,
				0.05f,
				minValue ? minValue->m_X : 0.0f,
				maxValue ? maxValue->m_X : 0.0f);
			value = current;
			break;
		}
		case LabParameterType::Color:
		{
			Color current = std::get<Color>(value);
			changed = ImGui::ColorEdit4(desc.m_Name.c_str(), &current.m_R);
			value = current;
			break;
		}
		}

		if (changed)
		{
			if (LabRuntime* runtime = m_RuntimeLocator ? m_RuntimeLocator->GetLabRuntimeIfCreated() : nullptr)
			{
				runtime->RequestSetParameter(desc.m_Id, value);
			}
		}
		return changed;
	}
}
