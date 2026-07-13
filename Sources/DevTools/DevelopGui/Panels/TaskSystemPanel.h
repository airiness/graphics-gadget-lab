#pragma once
#include "DevTools/DevelopGui/DevelopGuiPanel.h"

namespace gglab
{
	class TaskSystemPanel final : public DevelopGuiPanelBase
	{
	public:
		std::string_view GetPath() const noexcept override { return "Diagnostics/Task System"; }
		std::string_view GetTitle() const noexcept override { return "Task System"; }
		void Draw(DevelopGuiContext& context) noexcept override;
	};
}
