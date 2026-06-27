#pragma once
#include "DevTools/DevelopGui/DevelopGuiPanel.h"

namespace gglab
{
	class TransientResourcePoolPanel final : public DevelopGuiPanelBase
	{
	public:
		std::string_view GetPath() const noexcept override
		{
			return "Rendering/RenderGraph/Transient Resource Pool";
		}
		std::string_view GetTitle() const noexcept override { return "Transient Resource Pool"; }
		void Draw(DevelopGuiContext& context) noexcept override;
	};
}
