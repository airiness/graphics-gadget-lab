#pragma once
#include "DevTools/DevelopGui/DevelopGuiPanel.h"

namespace gglab
{
	class RenderGraphInspectorPanel final : public DevelopGuiPanelBase
	{
	public:
		std::string_view GetPath() const noexcept override
		{
			return "Diagnostics/Render Graph";
		}
		std::string_view GetTitle() const noexcept override { return "RenderGraph Inspector"; }
		void Draw(DevelopGuiContext& context) noexcept override;
	};
}
