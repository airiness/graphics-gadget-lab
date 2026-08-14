#pragma once

#include "DevTools/DevelopGui/DevelopGuiPanel.h"

namespace gglab
{
	class GTAOInspectorPanel final : public DevelopGuiPanelBase
	{
	public:
		std::string_view GetPath() const noexcept override
		{
			return "Rendering/Lighting/GTAO";
		}
		std::string_view GetTitle() const noexcept override { return "GTAO Inspector"; }
		void Draw(DevelopGuiContext& context) noexcept override;
	};
}
