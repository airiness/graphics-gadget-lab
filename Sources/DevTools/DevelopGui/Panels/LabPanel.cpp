#include "Core/Precompiled.h"
#include "DevTools/DevelopGui/Panels/LabPanel.h"
#include "Application/Lab/LabInterfaces.h"
#include "DevTools/DevelopGui/DevelopGuiContext.h"

namespace gglab
{
	namespace
	{
		const char* ToText(LabRuntimeState state) noexcept
		{
			switch (state)
			{
			case LabRuntimeState::Uninitialized:
				return "Uninitialized";
			case LabRuntimeState::Ready:
				return "Ready";
			case LabRuntimeState::Failed:
				return "Failed";
			}
			return "Unknown";
		}

		template<typename T>
		const T* GetOptionalValue(const std::optional<LabValue>& value) noexcept
		{
			return value ? std::get_if<T>(&*value) : nullptr;
		}
	}

	void LabPanel::Draw(DevelopGuiContext& context) noexcept
	{
		GGLAB_UNUSED(context);
		if (!m_Control || !m_SnapshotSource)
		{
			ImGui::TextDisabled("Lab runtime is not available.");
			return;
		}

		const LabSnapshot snapshot = m_SnapshotSource->GetLabSnapshot();
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
					m_Control->RequestSwitchLab(descriptor.m_Id);
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
		if (!snapshot.m_IsHostActive)
		{
			ImGui::TextDisabled("Demo.LabHost is inactive; commands will apply when it becomes active.");
		}
		if (!snapshot.m_Description.empty())
		{
			ImGui::TextWrapped("%s", snapshot.m_Description.c_str());
		}

		if (ImGui::Button("Reset Parameters"))
		{
			m_Control->RequestResetParameters();
			commandQueued = true;
		}
		ImGui::SameLine();
		if (ImGui::Button("Rebuild Scene"))
		{
			m_Control->RequestRebuildScene();
			commandQueued = true;
		}
		ImGui::SameLine();
		if (ImGui::Button("Restart Session"))
		{
			m_Control->RequestRestartSession();
			commandQueued = true;
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
				&current.x,
				0.05f,
				minValue ? minValue->x : 0.0f,
				maxValue ? maxValue->x : 0.0f);
			value = current;
			break;
		}
		case LabParameterType::Color:
		{
			Color current = std::get<Color>(value);
			changed = ImGui::ColorEdit4(desc.m_Name.c_str(), &current.x);
			value = current;
			break;
		}
		}

		if (changed)
		{
			m_Control->RequestSetParameter(desc.m_Id, value);
		}
		return changed;
	}
}
