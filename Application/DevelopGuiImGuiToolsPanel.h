#pragma once
#include "DevelopGuiContext.h"

#include <imgui.h>
#include <cstdint>

namespace gglab
{
	struct ImGuiToolsState
	{
		int32_t m_ClickCount = 0;
	};

	void DevelopGuiImGuiToolsPanel(DevelopGuiContext& context) noexcept
	{
		auto& state = context.PanelState<ImGuiToolsState>();

		ImGui::TextUnformatted("ImGui Tools");
		ImGui::Separator();

		ImGuiStorage& storage = *ImGui::GetStateStorage();
		ImGuiID demoId = ImGui::GetID("ShowDemo");
		ImGuiID metricsId = ImGui::GetID("ShowMetrics");

		bool isDemoShow = storage.GetBool(demoId, false);
		if (ImGui::Checkbox("Show ImGui Demo Window", &isDemoShow))
		{
			storage.SetBool(demoId, isDemoShow);
		}

		bool isMetricsShow = storage.GetBool(metricsId, false);
		if (ImGui::Checkbox("Show ImGui Metrics Window", &isMetricsShow))
		{
			storage.SetBool(metricsId, isMetricsShow);
		}

		ImGui::Separator();
		if (ImGui::Button("Click"))
		{
			++state.m_ClickCount;
		}
		ImGui::SameLine();
		ImGui::Text("Count: %d", state.m_ClickCount);

		// Controlled by window itself
		if (isDemoShow)
		{
			ImGui::ShowDemoWindow(&isDemoShow);
		}

		if (isMetricsShow)
		{
			ImGui::ShowMetricsWindow(&isMetricsShow);
		}

		// Write bool flag back to storage
		storage.SetBool(demoId, isDemoShow);
		storage.SetBool(metricsId, isMetricsShow);
	}
}