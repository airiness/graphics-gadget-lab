#pragma once
#include "DevTools/DevelopGui/DevelopGuiPanel.h"

namespace gglab
{
	class ImGuiToolsPanel final : public DevelopGuiPanelBase
	{
	public:
		std::string_view GetPath() const noexcept override { return "ImGui/Tools"; }
		std::string_view GetTitle() const noexcept override { return "ImGui Tools"; }
		void Draw(DevelopGuiContext& context) noexcept override;
	};
}
