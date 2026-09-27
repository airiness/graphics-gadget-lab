#pragma once

#include "DevTools/DevelopGui/DevelopGuiPanel.h"

namespace gglab
{
	class SceneDepthPanel final : public DevelopGuiPanelBase
	{
	public:
		std::string_view GetPath() const noexcept override { return "Diagnostics/Scene Depth"; }
		std::string_view GetTitle() const noexcept override { return "Scene Depth"; }
		void Draw(DevelopGuiContext& context) noexcept override;
	};
}
