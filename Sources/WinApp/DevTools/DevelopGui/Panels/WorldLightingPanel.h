#pragma once

#include "DevTools/DevelopGui/DevelopGuiPanel.h"
#include "DevTools/DevelopGui/Panels/IBLViewerPanel.h"

namespace gglab
{
	class WorldLightingPanel final : public DevelopGuiPanelBase
	{
	public:
		static constexpr std::string_view Path = "Scene/World Lighting";
		static constexpr std::string_view AtmosphereSection = "Atmosphere";
		std::string_view GetPath() const noexcept override { return Path; }
		std::string_view GetTitle() const noexcept override { return "World Lighting"; }
		void Draw(DevelopGuiContext& context) noexcept override;
		void RequestSection(std::string_view section) noexcept override
		{
			if (section == AtmosphereSection) m_AtmosphereRequested = true;
		}

	private:
		IBLViewerPanel m_EnvironmentPanel;
		bool m_AtmosphereRequested = false;
	};
}
