#pragma once

#include "DevTools/DevelopGui/DevelopGuiPanel.h"

namespace gglab
{
	class ForwardPlusInspectorPanel final : public DevelopGuiPanelBase
	{
	public:
		std::string_view GetPath() const noexcept override
		{
			return "Rendering/Forward+";
		}
		std::string_view GetTitle() const noexcept override { return "Forward+"; }
		void Draw(DevelopGuiContext& context) noexcept override;
	};
}
