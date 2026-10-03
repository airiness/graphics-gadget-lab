#include "DevTools/DevToolsRuntime.h"
#include "DevTools/DevelopGui/DevelopGuiContext.h"
#include "DevTools/DevelopGui/Panels/RenderingSettingsPanel.h"

#include <format>

#include <imgui.h>

namespace gglab
{
	uint32_t ViewRenderSettingsOverrides::GetActiveCount() const noexcept
	{
		return static_cast<uint32_t>(m_TemporalAA.m_IsActive) +
			static_cast<uint32_t>(m_GTAO.m_IsActive) +
			static_cast<uint32_t>(m_Bloom.m_IsActive) +
			static_cast<uint32_t>(m_ForwardLightingMode.has_value()) +
			static_cast<uint32_t>(m_ScenePreExposure.has_value()) +
			static_cast<uint32_t>(m_HdrDiffValidation.has_value());
	}

	void DevToolsRuntime::Reset() noexcept
	{
		m_Registry.Reset();
		m_ViewRenderSettingsOverrides.ClearAll();
	}

	ViewRenderProfile DevToolsRuntime::ResolveViewRenderProfile(
		const ViewRenderProfile& authoringProfile) const noexcept
	{
		ViewRenderProfile effectiveProfile = authoringProfile;
		effectiveProfile.m_EnableScenePreExposure =
			m_ViewRenderSettingsOverrides.m_ScenePreExposure.value_or(
				authoringProfile.m_EnableScenePreExposure);
		effectiveProfile.m_Lighting.m_ForwardPlus.m_Mode =
			m_ViewRenderSettingsOverrides.m_ForwardLightingMode.value_or(
				authoringProfile.m_Lighting.m_ForwardPlus.m_Mode);
		effectiveProfile.m_Lighting.m_ForwardPlus.m_EnableHdrDiffValidation =
			m_ViewRenderSettingsOverrides.m_HdrDiffValidation.value_or(
				authoringProfile.m_Lighting.m_ForwardPlus.m_EnableHdrDiffValidation);
		if (m_ViewRenderSettingsOverrides.m_TemporalAA.m_IsActive)
		{
			effectiveProfile.m_TemporalAA =
				m_ViewRenderSettingsOverrides.m_TemporalAA.m_Settings;
		}
		if (m_ViewRenderSettingsOverrides.m_GTAO.m_IsActive)
		{
			effectiveProfile.m_Lighting.m_GTAO = m_ViewRenderSettingsOverrides.m_GTAO.m_Settings;
		}
		if (m_ViewRenderSettingsOverrides.m_Bloom.m_IsActive)
		{
			effectiveProfile.m_PostProcess.m_Bloom =
				m_ViewRenderSettingsOverrides.m_Bloom.m_Settings;
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
		const uint32_t activeOverrides = m_ViewRenderSettingsOverrides.GetActiveCount();
		if (activeOverrides > 0 && ImGui::BeginMainMenuBar())
		{
			const auto label = std::format("{} DevTools Overrides Active", activeOverrides);
			ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.75f, 0.3f, 1.0f));
			if (ImGui::MenuItem(label.c_str()))
			{
				GGLAB_UNUSED(m_Registry.OpenPanel(RenderingSettingsPanel::Path));
			}
			ImGui::PopStyleColor();
			ImGui::EndMainMenuBar();
		}
		m_Registry.DrawPanels(context);
	}
}
