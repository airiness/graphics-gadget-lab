#pragma once
#include "DevTools/DevelopGui/DevelopGuiPanel.h"

namespace gglab
{
	class RenderViewPanel final : public DevelopGuiPanelBase
	{
	public:
		std::string_view GetPath() const noexcept override { return "Rendering/Frame/Render Views"; }
		std::string_view GetTitle() const noexcept override { return "RenderView"; }
		void Draw(DevelopGuiContext& context) noexcept override;
	};
}
