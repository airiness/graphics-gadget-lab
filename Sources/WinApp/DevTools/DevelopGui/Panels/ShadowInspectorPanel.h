#pragma once
#include "DevTools/DevelopGui/DevelopGuiPanel.h"

namespace gglab
{
	class ShadowInspectorPanel final : public DevelopGuiPanelBase
	{
	public:
		std::string_view GetPath() const noexcept override { return "Rendering/Lighting/Shadows"; }
		std::string_view GetTitle() const noexcept override { return "Shadow Inspector"; }
		void Draw(DevelopGuiContext& context) noexcept override;
	};
}
