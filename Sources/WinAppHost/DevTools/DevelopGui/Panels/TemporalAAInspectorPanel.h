#pragma once

#include "DevTools/DevelopGui/DevelopGuiPanel.h"

namespace gglab
{
	class TemporalAAInspectorPanel final : public DevelopGuiPanelBase
	{
	public:
		std::string_view GetPath() const noexcept override
		{
			return "Rendering/Temporal AA";
		}
		std::string_view GetTitle() const noexcept override { return "Temporal AA"; }
		void Draw(DevelopGuiContext& context) noexcept override;
	};
}
