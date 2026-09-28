#pragma once
#include "DevTools/DevelopGui/DevelopGuiPanel.h"

namespace gglab
{
	class IBLViewerPanel final : public DevelopGuiPanelBase
	{
	public:
		std::string_view GetPath() const noexcept override
		{
			return "Scene/World Lighting/Environment & IBL";
		}
		std::string_view GetTitle() const noexcept override { return "Environment & IBL"; }
		void Draw(DevelopGuiContext& context) noexcept override;
	};
}
