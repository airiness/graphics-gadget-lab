#pragma once
#include "DevTools/DevelopGui/DevelopGuiPanel.h"

namespace gglab
{
	class PostProcessInspectorPanel final : public DevelopGuiPanelBase
	{
	public:
		std::string_view GetPath() const noexcept override
		{
			return "Rendering/Post Processing";
		}
		std::string_view GetTitle() const noexcept override { return "Post Processing"; }
		void Draw(DevelopGuiContext& context) noexcept override;
	};
}
