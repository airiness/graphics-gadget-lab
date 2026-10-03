#pragma once

#include "DevTools/DevelopGui/DevelopGuiPanel.h"

namespace gglab
{
	class RenderingSettingsPanel final : public DevelopGuiPanelBase
	{
	public:
		static constexpr std::string_view Path = "Rendering/Rendering Settings";
		std::string_view GetPath() const noexcept override { return Path; }
		std::string_view GetTitle() const noexcept override { return "Rendering Settings"; }
		int32_t GetOrder() const noexcept override { return -100; }
		void Draw(DevelopGuiContext& context) noexcept override;
	};
}
