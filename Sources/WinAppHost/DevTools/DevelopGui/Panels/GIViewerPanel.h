#pragma once
#include "DevTools/DevelopGui/DevelopGuiPanel.h"
#include "GGLabRuntime/Graphics/Pipeline/GIProbePicking.h"
#include <array>
#include <optional>
#include <string>

namespace gglab
{
	class GIViewerPanel final : public DevelopGuiPanelBase
	{
	public:
		std::string_view GetPath() const noexcept override { return "Rendering/GI Viewer"; }
		std::string_view GetTitle() const noexcept override { return "GI Viewer"; }
		void Draw(DevelopGuiContext& context) noexcept override;
		void OnClosed(DevelopGuiContext& context) noexcept override;
		void OnFrame(DevelopGuiContext& context) noexcept override;

	private:
		bool m_Frozen = false;
		std::array<int, 3> m_ProbeCoordinate{};
		std::array<int, 2> m_ReceiverPixel{};
		bool m_ReceiverMarkers = true;
		int m_ReceiverCorner = 0;
		bool m_PickingEnabled = false;
		uint64_t m_PickToken = 0;
		std::optional<GIProbePickingRequest> m_PendingPick;
		GIProbeVisualizationSettings m_PickGuiSettings;
		std::optional<GIProbeSelection> m_PickEffectiveSelection;
		std::string m_PickContent;
		std::string m_PickStatus = "Enable picking, then click a probe in the scene.";
	};
}
