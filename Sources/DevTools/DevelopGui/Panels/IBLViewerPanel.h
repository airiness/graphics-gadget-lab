#pragma once
#include "DevTools/DevelopGui/DevelopGuiPanel.h"

namespace gglab
{
	class IBLViewerPanel final : public DevelopGuiPanelBase
	{
	public:
		std::string_view GetPath() const noexcept override { return "Rendering/PBR/IBL"; }
		std::string_view GetTitle() const noexcept override { return "IBL Viewer"; }
		void Draw(DevelopGuiContext& context) noexcept override;
	};
}
