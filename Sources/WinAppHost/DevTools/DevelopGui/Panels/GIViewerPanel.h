#pragma once
#include "DevTools/DevelopGui/DevelopGuiPanel.h"
#include <array>

namespace gglab
{
	class GIViewerPanel final : public DevelopGuiPanelBase
	{
	public:
		std::string_view GetPath() const noexcept override { return "Rendering/GI Viewer"; }
		std::string_view GetTitle() const noexcept override { return "GI Viewer"; }
		void Draw(DevelopGuiContext& context) noexcept override;

	private:
		bool m_Frozen = false;
		std::array<int, 3> m_ProbeCoordinate{};
	};
}
