#pragma once
#include "GGLabFoundation/Base/CoreMacros.h"
#include "DevTools/DevelopGui/DevelopGuiRegistry.h"
#include "GGLabRuntime/Graphics/PostProcess/ViewRenderSettings.h"
#include "GGLabRuntime/Graphics/ShadowSettings.h"

namespace gglab
{
	struct DevelopGuiContext;

	struct RenderVisualizationSettings
	{
		ShadowVisualizationSettings m_Shadow;
	};

	struct GTAOSettingsOverride
	{
		GTAOSettings m_Settings{};
		bool m_IsActive = false;
	};

	struct TemporalAASettingsOverride
	{
		TemporalAASettings m_Settings{};
		bool m_IsActive = false;
	};

	struct ViewRenderSettingsOverrides
	{
		TemporalAASettingsOverride m_TemporalAA{};
		GTAOSettingsOverride m_GTAO{};
	};

	class DevToolsRuntime
	{
	public:
		DevToolsRuntime() noexcept = default;
		GGLAB_DELETE_COPYABLE_MOVABLE(DevToolsRuntime);
		~DevToolsRuntime() = default;

		void Reset() noexcept;
		void Draw(DevelopGuiContext& context) noexcept;

		DevelopGuiRegistry& GetRegistry() noexcept { return m_Registry; }
		RenderVisualizationSettings& GetRenderVisualizationSettings() noexcept
		{
			return m_RenderVisualizationSettings;
		}
		const RenderVisualizationSettings& GetRenderVisualizationSettings() const noexcept
		{
			return m_RenderVisualizationSettings;
		}
		ViewRenderSettingsOverrides& GetViewRenderSettingsOverrides() noexcept
		{
			return m_ViewRenderSettingsOverrides;
		}
		const ViewRenderSettingsOverrides& GetViewRenderSettingsOverrides() const noexcept
		{
			return m_ViewRenderSettingsOverrides;
		}
		[[nodiscard]] ViewRenderProfile ResolveViewRenderProfile(
			const ViewRenderProfile& authoringProfile) const noexcept;

	private:
		DevelopGuiRegistry m_Registry;
		RenderVisualizationSettings m_RenderVisualizationSettings{};
		ViewRenderSettingsOverrides m_ViewRenderSettingsOverrides{};
	};
}
