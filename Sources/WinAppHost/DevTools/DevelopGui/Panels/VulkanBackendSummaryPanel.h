#pragma once
#include "DevTools/DevelopGui/DevelopGuiPanel.h"

namespace gglab
{
	class VulkanBackendSummaryPanel final : public DevelopGuiPanelBase
	{
	public:
		std::string_view GetPath() const noexcept override { return "Diagnostics/RHI"; }
		std::string_view GetTitle() const noexcept override { return "Vulkan Backend"; }
		void Draw(DevelopGuiContext& context) noexcept override;
		bool IsDefaultOpen() const noexcept override { return true; }
	};
}
