#include "DevTools/DevelopGui/Panels/LabPanel.h"
#include "Application/Lab/LabInterfaces.h"
#include "Application/Lab/LabRuntime.h"
#include "Diagnostics/DiagnosticsRuntime.h"
#include "DevTools/EnumText/EnumTextLab.h"
#include "DevTools/EnumText/EnumTextGraphics.h"
#include "DevTools/DevelopGui/DevelopGuiContext.h"

#include <imgui.h>

namespace gglab
{
	namespace
	{
		constexpr std::string_view CullingLabId = "gglab.lab.culling";

		template <typename T>
		const T* GetOptionalValue(const std::optional<LabSnapshotValue>& value) noexcept
		{
			return value ? std::get_if<T>(&*value) : nullptr;
		}

		LabRunConfig ToLabRunConfig(const LabRunConfigSnapshot& snapshot) noexcept
		{
			LabRunConfig config{
				.m_RandomSeed = snapshot.m_RandomSeed,
				.m_WarmupFrames = snapshot.m_WarmupFrames,
				.m_UseFixedDeltaTime = snapshot.m_UseFixedDeltaTime,
				.m_FixedDeltaTime = snapshot.m_FixedDeltaTime,
			};
			config.Sanitize();
			return config;
		}

		void DrawCullingLabStatistics(const DevelopGuiContext& context) noexcept
		{
			if (context.m_RenderQueues.empty())
			{
				ImGui::TextDisabled("Render queue statistics are not available yet.");
				return;
			}

			constexpr ImGuiTableFlags tableFlags = ImGuiTableFlags_Borders |
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
				if (stats.m_TotalInstanceCount == 0 && stats.m_VisibleInstanceCount == 0 &&
					stats.m_CulledInstanceCount == 0 && stats.m_InvalidInstanceCount == 0)
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

		void DrawLabDiagnostics(const LabDiagnosticsSnapshot& diagnostics) noexcept
		{
			if (diagnostics.m_Metrics.empty() && diagnostics.m_Checks.empty())
			{
				return;
			}

			ImGui::SeparatorText(
				diagnostics.m_Title.empty() ? "Verification" : diagnostics.m_Title.c_str());
			if (!diagnostics.m_Metrics.empty() &&
				ImGui::BeginTable("LabDiagnosticMetrics", 2,
					ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable))
			{
				ImGui::TableSetupColumn("Metric", ImGuiTableColumnFlags_WidthStretch);
				ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);
				ImGui::TableHeadersRow();
				for (const LabDiagnosticMetric& metric : diagnostics.m_Metrics)
				{
					ImGui::TableNextRow();
					ImGui::TableSetColumnIndex(0);
					ImGui::TextUnformatted(metric.m_Name.c_str());
					ImGui::TableSetColumnIndex(1);
					ImGui::TextUnformatted(metric.m_Value.c_str());
				}
				ImGui::EndTable();
			}

			for (const LabDiagnosticCheck& check : diagnostics.m_Checks)
			{
				const ImVec4 color = check.m_Status == LabDiagnosticCheckStatus::Passed
					? ImVec4(0.25f, 0.85f, 0.35f, 1.0f)
					: check.m_Status == LabDiagnosticCheckStatus::Failed
					? ImVec4(1.0f, 0.3f, 0.25f, 1.0f)
					: ImVec4(0.95f, 0.75f, 0.2f, 1.0f);
				const char* status = check.m_Status == LabDiagnosticCheckStatus::Passed ? "PASS"
					: check.m_Status == LabDiagnosticCheckStatus::Failed
					? "FAIL"
					: "PENDING";
				ImGui::TextColored(color, "[%s]", status);
				ImGui::SameLine();
				ImGui::TextUnformatted(check.m_Name.c_str());
				if (!check.m_Detail.empty())
				{
					ImGui::Indent();
					ImGui::TextWrapped("%s", check.m_Detail.c_str());
					ImGui::Unindent();
				}
			}
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
			ImGui::TextDisabled("Demo.LabHost is not active or preparing.");
			return;
		}

		LabSnapshot sourceSnapshot{};
		const LabSnapshot* snapshotPtr =
			context.m_Diagnostics ? context.m_Diagnostics->GetSnapshot<LabSnapshot>() : nullptr;
		if (!snapshotPtr)
		{
			sourceSnapshot = runtime->GetLabSnapshot();
			snapshotPtr = &sourceSnapshot;
		}
		const LabSnapshot& snapshot = *snapshotPtr;
		if (!m_DeferredParameterEdits.empty() &&
			m_DeferredParameterEdits.front().m_LabId != snapshot.m_ActiveLabId)
		{
			m_DeferredParameterEdits.clear();
		}
		bool commandQueued = false;

		ImGui::TextUnformatted("Active Lab");
		ImGui::SetNextItemWidth(-FLT_MIN);
		const char* activeName =
			snapshot.m_ActiveLabName.empty() ? "None" : snapshot.m_ActiveLabName.c_str();
		if (ImGui::BeginCombo("##ActiveLab", activeName))
		{
			for (const LabDescriptorSnapshot& descriptor : snapshot.m_AvailableLabs)
			{
				const bool selected = descriptor.m_Id == snapshot.m_ActiveLabId;
				ImGui::PushID(descriptor.m_Id.m_Name.c_str());
				if (ImGui::Selectable(descriptor.m_DisplayName.c_str(), selected))
				{
					runtime->RequestSwitchLab(LabId(descriptor.m_Id.m_Name));
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

		const std::string runState = devtools::EnumText(snapshot.m_State);
		ImGui::Text("State: %s", runState.c_str());
		if (snapshot.m_HasPendingSession)
		{
			ImGui::Text("Pending Lab: %s", snapshot.m_PendingLabName.c_str());
			const float fraction = std::clamp(snapshot.m_LoadingFraction, 0.0f, 1.0f);
			const std::string percentage =
				std::format("{}%", static_cast<int32_t>(std::round(fraction * 100.0f)));
			ImGui::ProgressBar(fraction, ImVec2(-FLT_MIN, 0.0f), percentage.c_str());
			if (!snapshot.m_LoadingStage.empty())
			{
				ImGui::TextUnformatted(snapshot.m_LoadingStage.c_str());
			}
			if (!snapshot.m_LoadingDetail.empty())
			{
				ImGui::TextWrapped("%s", snapshot.m_LoadingDetail.c_str());
			}
		}
		if (snapshot.m_RetiringSessionCount > 0)
		{
			ImGui::TextDisabled(
				"Retiring sessions: %u (waiting for GPU fences)", snapshot.m_RetiringSessionCount);
		}
		ImGui::Text("Session frame: %llu", snapshot.m_FrameInSession);
		if (snapshot.m_State == LabSnapshotRunState::WarmingUp)
		{
			ImGui::Text("Warm-up remaining: %u", snapshot.m_WarmupFramesRemaining);
		}
		ImGui::Text("Effective delta: %.6f s", snapshot.m_EffectiveDeltaTime);
		if (snapshot.m_LastFrame.m_HasFeedback)
		{
			ImGui::Text("Last submitted frame: %llu (back buffer %u, fence %llu)",
				snapshot.m_LastFrame.m_ApplicationFrameIndex,
				snapshot.m_LastFrame.m_BackBufferIndex, snapshot.m_LastFrame.m_SubmittedFenceValue);
		}
		if (!snapshot.m_IsHostActive)
		{
			ImGui::TextDisabled(
				"Demo.LabHost is inactive; commands will apply when it becomes active.");
		}
		if (!snapshot.m_Description.empty())
		{
			ImGui::TextWrapped("%s", snapshot.m_Description.c_str());
		}

		if (snapshot.m_ActiveLabId.m_Name == CullingLabId)
		{
			ImGui::SeparatorText("Culling Statistics");
			DrawCullingLabStatistics(context);
		}
		DrawLabDiagnostics(snapshot.m_Diagnostics);

		if (ImGui::Button("Reset Parameters"))
		{
			m_DeferredParameterEdits.clear();
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
			LabRunConfigSnapshot& config = *m_RunConfigDraft;
			ImGui::InputScalar("Random Seed", ImGuiDataType_U64, &config.m_RandomSeed);
			ImGui::DragScalar("Warm-up Frames", ImGuiDataType_U32, &config.m_WarmupFrames, 1.0f);
			ImGui::Checkbox("Use Fixed Delta Time", &config.m_UseFixedDeltaTime);
			ImGui::BeginDisabled(!config.m_UseFixedDeltaTime);
			ImGui::DragFloat("Fixed Delta Time", &config.m_FixedDeltaTime, 0.0001f, 1.0f / 1000.0f,
				1.0f, "%.6f s");
			ImGui::EndDisabled();
			if (ImGui::Button("Restart with Run Config"))
			{
				runtime->RequestRunConfig(ToLabRunConfig(config));
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
			if (DrawParameter(snapshot.m_ActiveLabId, parameter))
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

	bool LabPanel::DrawParameter(const LabIdSnapshot& activeLabId,
		const LabParameterSnapshot& parameter) noexcept
	{
		const LabParameterDescSnapshot& desc = parameter.m_Desc;
		const bool deferred =
			desc.m_EditPolicy == LabSnapshotParameterEditPolicy::CommitOnEditEnd;
		auto draft = std::ranges::find_if(m_DeferredParameterEdits,
			[&activeLabId, &desc](const DeferredParameterEdit& edit) noexcept
			{
				return edit.m_LabId == activeLabId && edit.m_ParameterId == desc.m_Id;
			});
		LabSnapshotValue value = draft != m_DeferredParameterEdits.end()
			? draft->m_Value
			: parameter.m_Value;
		bool changed = false;

		switch (desc.m_Type)
		{
		case LabSnapshotParameterType::Bool:
		{
			bool current = std::get<bool>(value);
			changed = ImGui::Checkbox(desc.m_Name.c_str(), &current);
			value = current;
			break;
		}
		case LabSnapshotParameterType::Int:
		{
			int32_t current = std::get<int32_t>(value);
			const int32_t* minValue = GetOptionalValue<int32_t>(desc.m_MinValue);
			const int32_t* maxValue = GetOptionalValue<int32_t>(desc.m_MaxValue);
			changed = ImGui::DragInt(desc.m_Name.c_str(), &current, 1.0f, minValue ? *minValue : 0,
				maxValue ? *maxValue : 0);
			value = current;
			break;
		}
		case LabSnapshotParameterType::UInt:
		{
			uint32_t current = std::get<uint32_t>(value);
			const uint32_t* minValue = GetOptionalValue<uint32_t>(desc.m_MinValue);
			const uint32_t* maxValue = GetOptionalValue<uint32_t>(desc.m_MaxValue);
			changed = ImGui::DragScalar(
				desc.m_Name.c_str(), ImGuiDataType_U32, &current, 1.0f, minValue, maxValue);
			value = current;
			break;
		}
		case LabSnapshotParameterType::Float:
		{
			float current = std::get<float>(value);
			const float* minValue = GetOptionalValue<float>(desc.m_MinValue);
			const float* maxValue = GetOptionalValue<float>(desc.m_MaxValue);
			changed = ImGui::DragFloat(desc.m_Name.c_str(), &current, 0.05f,
				minValue ? *minValue : 0.0f, maxValue ? *maxValue : 0.0f);
			value = current;
			break;
		}
		case LabSnapshotParameterType::Enum:
		{
			int32_t current = std::get<int32_t>(value);
			const auto selected = std::ranges::find_if(desc.m_EnumItems,
				[current](const LabEnumItemSnapshot& item) { return item.m_Value == current; });
			const char* preview =
				selected != desc.m_EnumItems.end() ? selected->m_Name.c_str() : "Unknown";
			if (ImGui::BeginCombo(desc.m_Name.c_str(), preview))
			{
				for (const LabEnumItemSnapshot& item : desc.m_EnumItems)
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
		case LabSnapshotParameterType::Vector3:
		{
			Vector3 current = std::get<Vector3>(value);
			const Vector3* minValue = GetOptionalValue<Vector3>(desc.m_MinValue);
			const Vector3* maxValue = GetOptionalValue<Vector3>(desc.m_MaxValue);
			changed = ImGui::DragFloat3(desc.m_Name.c_str(), &current.m_X, 0.05f,
				minValue ? minValue->m_X : 0.0f, maxValue ? maxValue->m_X : 0.0f);
			value = current;
			break;
		}
		case LabSnapshotParameterType::Color:
		{
			Color current = std::get<Color>(value);
			changed = ImGui::ColorEdit4(desc.m_Name.c_str(), &current.m_R);
			value = current;
			break;
		}
		}

		if (deferred)
		{
			if (changed)
			{
				if (draft != m_DeferredParameterEdits.end())
				{
					draft->m_Value = value;
				}
				else
				{
					m_DeferredParameterEdits.push_back({
						.m_LabId = activeLabId,
						.m_ParameterId = desc.m_Id,
						.m_Value = value,
						});
					draft = std::prev(m_DeferredParameterEdits.end());
				}
			}
			if (ImGui::IsItemDeactivatedAfterEdit() && draft != m_DeferredParameterEdits.end())
			{
				if (LabRuntime* runtime =
					m_RuntimeLocator ? m_RuntimeLocator->GetLabRuntimeIfCreated() : nullptr)
				{
					runtime->RequestSetParameter(
						LabParameterId(desc.m_Id.m_Name), draft->m_Value);
					m_DeferredParameterEdits.erase(draft);
					return true;
				}
			}
			return false;
		}

		if (changed)
		{
			if (LabRuntime* runtime =
				m_RuntimeLocator ? m_RuntimeLocator->GetLabRuntimeIfCreated() : nullptr)
			{
				runtime->RequestSetParameter(LabParameterId(desc.m_Id.m_Name), value);
			}
		}
		return changed;
	}
}
