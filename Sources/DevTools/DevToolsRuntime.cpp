#include "DevTools/DevToolsRuntime.h"
#include "DevTools/DevelopGui/DevelopGuiContext.h"
#include "Diagnostics/Builders/BuiltinSnapshotProviders.h"

#include <imgui.h>

namespace gglab
{
	DevToolsRuntime::DevToolsRuntime() noexcept
	{
		RegisterBuiltinSnapshotProviders(m_Diagnostics);
	}

	void DevToolsRuntime::Reset() noexcept
	{
		m_Registry.Reset();
		m_Diagnostics.Reset();
		m_ViewRenderSettingsOverrides = {};
	}

	ViewRenderProfile DevToolsRuntime::ResolveViewRenderProfile(
		const ViewRenderProfile& authoringProfile) const noexcept
	{
		ViewRenderProfile effectiveProfile = authoringProfile;
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
		context.m_Diagnostics = &m_Diagnostics;
		m_Diagnostics.BeginFrame({
			.m_Renderer = context.m_Renderer,
			.m_AssetManager = context.m_AssetManager,
			.m_EnvironmentAssetController = context.m_EnvironmentAssetController,
			.m_TaskSystem = m_TaskSystem,
			.m_World = context.m_World,
			.m_RenderGraph = context.m_RenderGraph,
			.m_RenderViews = context.m_RenderViews,
			.m_MainRenderView = context.m_MainRenderView,
			.m_AuthoringViewRenderProfile = context.m_AuthoringViewRenderProfile,
			.m_EffectiveViewRenderProfile = context.m_EffectiveViewRenderProfile,
			.m_GTAOOverrideActive = m_ViewRenderSettingsOverrides.m_GTAO.m_IsActive,
			});

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
