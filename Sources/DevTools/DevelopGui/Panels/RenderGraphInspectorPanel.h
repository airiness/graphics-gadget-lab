#pragma once
#include "DevTools/DevelopGui/DevelopGuiPanel.h"

namespace gglab
{
	class RenderGraphInspectorPanel final : public DevelopGuiPanelBase
	{
	public:
		std::string_view GetPath() const noexcept override { return "Rendering/RenderGraph/Inspector"; }
		std::string_view GetTitle() const noexcept override { return "RenderGraph Inspector"; }
		void Draw(DevelopGuiContext& context) noexcept override;
	};
}
