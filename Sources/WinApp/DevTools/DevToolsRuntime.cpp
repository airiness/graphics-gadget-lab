#include "DevTools/DevToolsRuntime.h"
#include "DevTools/DevelopGui/DevelopGuiContext.h"

#include <imgui.h>

namespace gglab
{
	uint32_t ViewRenderSettingsOverrides::GetActiveCount() const noexcept
	{
		return static_cast<uint32_t>(m_TemporalAA.m_IsActive) +
			static_cast<uint32_t>(m_GTAO.m_IsActive) +
			static_cast<uint32_t>(m_Bloom.m_IsActive) +
			static_cast<uint32_t>(m_ForwardLightingMode.has_value()) +
			static_cast<uint32_t>(m_AerialPerspective.has_value()) +
			static_cast<uint32_t>(m_ScenePreExposure.has_value()) +
			static_cast<uint32_t>(m_HdrDiffValidation.has_value()) +
			static_cast<uint32_t>(m_AerialProbe.has_value());
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
		effectiveProfile.m_Lighting.m_EnableAerialPerspective =
			m_ViewRenderSettingsOverrides.m_AerialPerspective.value_or(
				authoringProfile.m_Lighting.m_EnableAerialPerspective);
		effectiveProfile.m_Lighting.m_EnableAerialProbe =
			m_ViewRenderSettingsOverrides.m_AerialProbe.value_or(
				authoringProfile.m_Lighting.m_EnableAerialProbe);
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
		m_Registry.DrawPanels(context);
	}
}
