#include "DevTools/DevToolsRuntime.h"
#include "DevTools/DevelopGui/DevelopGuiContext.h"

#include <imgui.h>

namespace gglab
{
	void DevToolsRuntime::Reset() noexcept
	{
		m_Registry.Reset();
		m_ViewRenderSettingsOverrides = {};
	}

	ViewRenderProfile DevToolsRuntime::ResolveViewRenderProfile(
		const ViewRenderProfile& authoringProfile) const noexcept
	{
		ViewRenderProfile effectiveProfile = authoringProfile;
		if (m_ViewRenderSettingsOverrides.m_TemporalAA.m_IsActive)
		{
			effectiveProfile.m_TemporalAA =
				m_ViewRenderSettingsOverrides.m_TemporalAA.m_Settings;
		}
		if (m_ViewRenderSettingsOverrides.m_GTAO.m_IsActive)
		{
			effectiveProfile.m_Lighting.m_GTAO = m_ViewRenderSettingsOverrides.m_GTAO.m_Settings;
		}
		return effectiveProfile;
	}

	void DevToolsRuntime::Draw(DevelopGuiContext& context) noexcept
	{
		context.m_ShadowVisualizationSettings = &m_RenderVisualizationSettings.m_Shadow;
		context.m_ViewRenderSettingsOverrides = &m_ViewRenderSettingsOverrides;

		ImGuiIO& io = ImGui::GetIO();
		if (io.ConfigFlags & ImGuiConfigFlags_DockingEnable)
		{
			ImGui::DockSpaceOverViewport(
				0, ImGui::GetMainViewport(), ImGuiDockNodeFlags_PassthruCentralNode);
		}

		m_Registry.DrawMenuBar();
		m_Registry.DrawPanels(context);
	}
}
