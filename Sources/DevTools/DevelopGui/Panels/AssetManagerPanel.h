#pragma once
#include "DevTools/DevelopGui/DevelopGuiPanel.h"

namespace gglab
{
	class AssetManagerPanel final : public DevelopGuiPanelBase
	{
	public:
		std::string_view GetPath() const noexcept override { return "Application/Assets"; }
		std::string_view GetTitle() const noexcept override { return "Asset Manager"; }
		void Draw(DevelopGuiContext& context) noexcept override;
	};
}
