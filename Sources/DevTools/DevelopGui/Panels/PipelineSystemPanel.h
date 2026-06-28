#pragma once
#include "DevTools/DevelopGui/DevelopGuiPanel.h"

namespace gglab
{
	class PipelineSystemPanel final : public DevelopGuiPanelBase
	{
	public:
		std::string_view GetPath() const noexcept override
		{
			return "Rendering/Pipeline System";
		}
		std::string_view GetTitle() const noexcept override { return "Pipeline System"; }
		void Draw(DevelopGuiContext& context) noexcept override;
	};
}
