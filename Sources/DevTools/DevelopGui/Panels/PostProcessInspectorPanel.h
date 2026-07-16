#pragma once
#include "DevTools/DevelopGui/DevelopGuiPanel.h"

namespace gglab
{
	class PostProcessInspectorPanel final : public DevelopGuiPanelBase
	{
	public:
		std::string_view GetPath() const noexcept override { return "Rendering/Post Process/Inspector"; }
		std::string_view GetTitle() const noexcept override { return "Post Process Inspector"; }
		void Draw(DevelopGuiContext& context) noexcept override;
	};
}
