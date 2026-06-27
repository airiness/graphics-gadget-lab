#pragma once
#include "DevTools/DevelopGui/DevelopGuiPanel.h"

namespace gglab
{
	class PersistentSceneBuffersPanel final : public DevelopGuiPanelBase
	{
	public:
		std::string_view GetPath() const noexcept override
		{
			return "Rendering/Buffers/Persistent Scene Buffers";
		}
		std::string_view GetTitle() const noexcept override { return "Persistent Scene Buffers"; }
		void Draw(DevelopGuiContext& context) noexcept override;
	};
}
