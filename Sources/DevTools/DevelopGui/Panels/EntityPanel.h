#pragma once
#include "DevTools/DevelopGui/DevelopGuiPanel.h"

namespace gglab
{
	class EntityPanel final : public DevelopGuiPanelBase
	{
	public:
		std::string_view GetPath() const noexcept override { return "Application/Entity"; }
		std::string_view GetTitle() const noexcept override { return "Entity"; }
		void Draw(DevelopGuiContext& context) noexcept override;
	};
}
