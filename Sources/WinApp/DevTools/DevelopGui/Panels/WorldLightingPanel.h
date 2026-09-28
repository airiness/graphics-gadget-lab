#pragma once

#include "DevTools/DevelopGui/DevelopGuiPanel.h"
#include "DevTools/DevelopGui/Panels/IBLViewerPanel.h"

namespace gglab
{
	class WorldLightingPanel final : public DevelopGuiPanelBase
	{
	public:
		std::string_view GetPath() const noexcept override { return "Scene/World Lighting"; }
		std::string_view GetTitle() const noexcept override { return "World Lighting"; }
		void Draw(DevelopGuiContext& context) noexcept override;

	private:
		IBLViewerPanel m_EnvironmentPanel;
	};
}
