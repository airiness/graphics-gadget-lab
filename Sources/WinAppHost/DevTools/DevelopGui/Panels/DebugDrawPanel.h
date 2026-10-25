#pragma once
#include "DevTools/DevelopGui/DevelopGuiPanel.h"

namespace gglab
{
	class DebugDrawPanel final : public DevelopGuiPanelBase
	{
	public:
		std::string_view GetPath() const noexcept override { return "Diagnostics/Debug Draw"; }
		std::string_view GetTitle() const noexcept override { return "DebugDraw"; }
		void Draw(DevelopGuiContext& context) noexcept override;
	};
}
