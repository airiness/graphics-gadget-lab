#pragma once
#include "GGLabFoundation/Base/CoreMacros.h"
#include "DevTools/DevelopGui/DevelopGuiRegistry.h"
#include "GGLabRuntime/Graphics/ViewRenderSettings.h"
#include "GGLabRuntime/Graphics/ShadowSettings.h"

#include <cstdint>
#include <optional>

namespace gglab
{
	struct DevelopGuiContext;

	struct RenderVisualizationSettings
	{
		ShadowVisualizationSettings m_Shadow;
	};

	template <typename T> struct SettingsOverride
	{
		T m_Settings{};
		bool m_IsActive = false;

		void Activate(const T& settings) noexcept
		{
			m_Settings = settings;
			m_IsActive = true;
		}
		void Reset() noexcept { *this = {}; }
	};

	// DevTools-session state: overrides survive Demo/Lab switches, but are never
	// persisted. Active blocks replace all parameters; absent scalars inherit.
	struct ViewRenderSettingsOverrides
	{
		SettingsOverride<TemporalAASettings> m_TemporalAA{};
		SettingsOverride<GTAOSettings> m_GTAO{};
		SettingsOverride<BloomSettings> m_Bloom{};
		std::optional<bool> m_ScenePreExposure;

		// Counts active blocks/scalars, including values equal to authoring settings.
		[[nodiscard]] uint32_t GetActiveCount() const noexcept;
		void ClearAll() noexcept { *this = {}; }
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
