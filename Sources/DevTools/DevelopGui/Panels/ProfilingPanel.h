#pragma once
#include "DevTools/DevelopGui/DevelopGuiPanel.h"

namespace gglab
{
	class ProfilingPanel final : public DevelopGuiPanelBase
	{
	public:
		std::string_view GetPath() const noexcept override { return "Diagnostics/Profiling"; }
		std::string_view GetTitle() const noexcept override { return "Profiling"; }
		void Draw(DevelopGuiContext& context) noexcept override;
	};
}
