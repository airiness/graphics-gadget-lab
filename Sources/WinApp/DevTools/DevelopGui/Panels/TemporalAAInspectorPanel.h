#pragma once

#include "DevTools/DevelopGui/DevelopGuiPanel.h"

namespace gglab
{
	class TemporalAAInspectorPanel final : public DevelopGuiPanelBase
	{
	public:
		std::string_view GetPath() const noexcept override
		{
			return "Rendering/Temporal AA/Inspector";
		}
		std::string_view GetTitle() const noexcept override { return "Temporal AA Inspector"; }
		void Draw(DevelopGuiContext& context) noexcept override;
	};
}
