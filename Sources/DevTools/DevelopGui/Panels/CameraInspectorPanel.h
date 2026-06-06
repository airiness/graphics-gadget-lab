#pragma once
#include "DevTools/DevelopGui/DevelopGuiPanel.h"

namespace gglab
{
	class CameraInspectorPanel final : public DevelopGuiPanelBase
	{
	public:
		std::string_view GetPath() const noexcept override { return "Application/Camera"; }
		std::string_view GetTitle() const noexcept override { return "Camera"; }
		void Draw(DevelopGuiContext& context) noexcept override;
	};
}
