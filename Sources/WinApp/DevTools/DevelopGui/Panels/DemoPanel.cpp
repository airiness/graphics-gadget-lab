#include "DevTools/DevelopGui/Panels/DemoPanel.h"
#include "Demo/DemoManager.h"
#include "DevTools/DevelopGui/DevelopGuiContext.h"

#include <imgui.h>

namespace gglab
{
	void DemoPanel::Draw(DevelopGuiContext& context) noexcept
	{
		GGLAB_UNUSED(context);
		const ImVec2 windowSize = ImGui::GetWindowSize();
		if (windowSize.x < 360.0f || windowSize.y < 120.0f)
		{
			ImGui::SetWindowSize(
				ImVec2(std::max(windowSize.x, 360.0f), std::max(windowSize.y, 120.0f)));
		}
		if (!m_DemoManager)
		{
			ImGui::TextDisabled("Demo manager is not available.");
			return;
		}

		const uint32_t activeIndex = m_DemoManager->GetActiveIndex();
		const uint32_t selectedIndex = m_DemoManager->HasPendingActiveDemo()
			? m_DemoManager->GetPendingActiveIndex()
			: activeIndex;
		const std::string preview = selectedIndex < m_DemoManager->GetDemoCount()
			? std::string(m_DemoManager->GetDemoName(selectedIndex))
			: std::string("None");

		ImGui::TextUnformatted("Active Demo");
		ImGui::SetNextItemWidth(-FLT_MIN);
		if (ImGui::BeginCombo("##ActiveDemo", preview.c_str()))
		{
			for (uint32_t index = 0; index < m_DemoManager->GetDemoCount(); ++index)
			{
				const std::string name(m_DemoManager->GetDemoName(index));
				const bool selected = index == selectedIndex;
				ImGui::PushID(static_cast<int>(index));
				if (ImGui::Selectable(name.c_str(), selected))
				{
					m_DemoManager->RequestActiveDemo(index);
				}
				if (selected)
				{
					ImGui::SetItemDefaultFocus();
				}
				ImGui::PopID();
			}
			ImGui::EndCombo();
		}

		if (m_DemoManager->HasPendingActiveDemo())
		{
			ImGui::Text("Pending Demo: %s", preview.c_str());
		}
		if (const auto progress = m_DemoManager->GetLoadingProgress())
		{
			const float fraction = std::clamp(progress->m_Fraction, 0.0f, 1.0f);
			const std::string percentage =
				std::format("{}%", static_cast<int32_t>(std::round(fraction * 100.0f)));
			ImGui::ProgressBar(fraction, ImVec2(-FLT_MIN, 0.0f), percentage.c_str());
			if (!progress->m_Stage.empty())
			{
				ImGui::TextUnformatted(progress->m_Stage.c_str());
			}
			if (!progress->m_Detail.empty())
			{
				ImGui::TextWrapped("%s", progress->m_Detail.c_str());
			}
		}
		if (m_DemoManager->GetRetiringDemoCount() > 0)
		{
			ImGui::TextDisabled("Retiring demos: %u (waiting for GPU fences)",
				m_DemoManager->GetRetiringDemoCount());
		}
	}
}
