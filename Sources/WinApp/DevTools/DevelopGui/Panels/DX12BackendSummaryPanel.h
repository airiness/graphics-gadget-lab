#pragma once
#include "DevTools/DevelopGui/DevelopGuiPanel.h"

namespace gglab
{
	class DX12BackendSummaryPanel final : public DevelopGuiPanelBase
	{
	public:
		std::string_view GetPath() const noexcept override { return "Diagnostics/RHI"; }
		std::string_view GetTitle() const noexcept override { return "DirectX 12 Backend"; }
		void Draw(DevelopGuiContext& context) noexcept override;
		bool IsDefaultOpen() const noexcept override { return true; }
	};
}
