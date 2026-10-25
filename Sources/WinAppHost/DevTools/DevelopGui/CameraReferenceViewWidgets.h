#pragma once
#include <cstdint>
#include <optional>
#include <string>

namespace gglab
{
	struct CameraToolingSnapshot;
	struct LabSnapshot;
	class CameraToolingControlBase;

	struct CameraReferenceViewWidgetState
	{
		uint64_t m_MainCameraId = 0;
		std::string m_SelectedReferenceId;
	};

	bool ReferenceViewsBelongInLabPanel(const LabSnapshot& lab) noexcept;
	std::optional<uint64_t> DrawCameraReferenceViews(CameraReferenceViewWidgetState& state,
		const CameraToolingSnapshot& snapshot, CameraToolingControlBase* control) noexcept;
}
