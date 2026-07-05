#include "Core/Precompiled.h"
#include "DevTools/DevelopGui/Panels/DemoPanel.h"
#include "Application/Demo/DemoManager.h"
#include "DevTools/DevelopGui/DevelopGuiContext.h"

namespace gglab
{
	void DemoPanel::Draw(DevelopGuiContext& context) noexcept
	{
		GGLAB_UNUSED(context);
		const ImVec2 windowSize = ImGui::GetWindowSize();
		if (windowSize.x < 360.0f || windowSize.y < 120.0f)
		{
			ImGui::SetWindowSize(ImVec2(
				std::max(windowSize.x, 360.0f),
				std::max(windowSize.y, 120.0f)));
		}
		if (!m_DemoManager)
		{
			ImGui::TextDisabled("Demo manager is not available.");
			return;
		}

		const uint32_t activeIndex = m_DemoManager->GetActiveIndex();
		const uint32_t selectedIndex = m_DemoManager->HasPendingActiveDemo() ?
			m_DemoManager->GetPendingActiveIndex() : activeIndex;
		const DemoBase* selectedDemo = m_DemoManager->GetDemo(selectedIndex);
		const std::string preview = selectedDemo ?
			std::string(selectedDemo->GetName()) : std::string("None");

		ImGui::SetNextItemWidth(-FLT_MIN);
		if (ImGui::BeginCombo("Active Demo", preview.c_str()))
		{
			for (uint32_t index = 0; index < m_DemoManager->GetDemoCount(); ++index)
			{
				const DemoBase* demo = m_DemoManager->GetDemo(index);
				if (!demo)
				{
					continue;
				}

				const std::string name(demo->GetName());
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
			ImGui::TextDisabled("The selection will be applied at the next frame boundary.");
		}
	}
}
