#include "DevTools/DevToolsRuntime.h"
#include "DevTools/DevelopGui/DevelopGuiContext.h"
#include "DevTools/DevelopGui/Panels/RenderingSettingsPanel.h"

#include <format>
#include <optional>

#include <imgui.h>

namespace gglab
{
	uint32_t ViewRenderSettingsOverrides::GetActiveCount() const noexcept
	{
		return static_cast<uint32_t>(m_TemporalAA.m_IsActive) +
			static_cast<uint32_t>(m_GTAO.m_IsActive) +
			static_cast<uint32_t>(m_Bloom.m_IsActive) +
			static_cast<uint32_t>(m_ScenePreExposure.has_value());
	}

	void DevToolsRuntime::Reset() noexcept
	{
		m_Registry.Reset();
		m_ViewRenderSettingsOverrides.ClearAll();
	}

	ApplicationToolingFrameSettings DevToolsRuntime::CaptureFrameSettings() const noexcept
	{
		const auto& overrides = m_ViewRenderSettingsOverrides;
		return {
			.m_TemporalAA = overrides.m_TemporalAA.m_IsActive ?
				std::optional(overrides.m_TemporalAA.m_Settings) :
				std::nullopt,
			.m_GTAO = overrides.m_GTAO.m_IsActive ?
				std::optional(overrides.m_GTAO.m_Settings) :
				std::nullopt,
			.m_Bloom = overrides.m_Bloom.m_IsActive ?
				std::optional(overrides.m_Bloom.m_Settings) :
				std::nullopt,
			.m_ScenePreExposure = overrides.m_ScenePreExposure,
			.m_Shadow = m_RenderVisualizationSettings.m_Shadow,
		};
	}

	ViewRenderProfile DevToolsRuntime::ResolveViewRenderProfile(
		const ViewRenderProfile& authoringProfile) const noexcept
	{
		return CaptureFrameSettings().ResolveProfile(authoringProfile);
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
