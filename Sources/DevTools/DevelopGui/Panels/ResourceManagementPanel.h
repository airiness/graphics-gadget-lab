#pragma once
#include "DevTools/DevelopGui/DevelopGuiPanel.h"

namespace gglab
{
	class ResourceManagementPanel final : public DevelopGuiPanelBase
	{
	public:
		std::string_view GetPath() const noexcept override { return "Rendering/Resources"; }
		std::string_view GetTitle() const noexcept override { return "Resource Management"; }
		void Draw(DevelopGuiContext& context) noexcept override;
	};
}
