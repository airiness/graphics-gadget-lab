#include "Core/Precompiled.h"
#include "DevTools/DevToolsRuntime.h"
#include "DevTools/DevelopGui/DevelopGuiContext.h"
#include "Diagnostics/Builders/BuiltinSnapshotProviders.h"

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
	}

	void DevToolsRuntime::Draw(DevelopGuiContext& context) noexcept
	{
		context.m_ShadowVisualizationSettings = &m_RenderVisualizationSettings.m_Shadow;
		context.m_Diagnostics = &m_Diagnostics;
		m_Diagnostics.BeginFrame({
			.m_Renderer = context.m_Renderer,
			.m_AssetManager = context.m_AssetManager,
			.m_World = context.m_World,
			.m_RenderGraph = context.m_RenderGraph,
			.m_RenderViews = context.m_RenderViews,
			.m_MainRenderView = context.m_MainRenderView,
		});

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
