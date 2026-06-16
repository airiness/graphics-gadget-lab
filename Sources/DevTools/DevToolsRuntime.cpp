#include "Core/Precompiled.h"
#include "DevTools/DevToolsRuntime.h"
#include "DevTools/DevelopGui/DevelopGuiContext.h"

namespace gglab
{
	void DevToolsRuntime::Reset() noexcept
	{
		m_Registry.Reset();
	}

	void DevToolsRuntime::Draw(DevelopGuiContext& context) noexcept
	{
		context.m_ShadowVisualizationSettings = &m_RenderVisualizationSettings.m_Shadow;

		ImGuiIO& io = ImGui::GetIO();
		if (io.ConfigFlags & ImGuiConfigFlags_DockingEnable)
		{
			ImGui::DockSpaceOverViewport(0,
				ImGui::GetMainViewport(),
				ImGuiDockNodeFlags_PassthruCentralNode);
		}

		m_Registry.DrawMenuBar();
		m_Registry.DrawPanels(context);
	}
}
