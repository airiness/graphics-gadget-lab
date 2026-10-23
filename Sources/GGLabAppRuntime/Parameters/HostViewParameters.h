#pragma once
#include "Lab/LabParameter.h"
#include "GGLabRuntime/Graphics/ViewRenderSettings.h"

#include <optional>

namespace gglab
{
	struct ViewRenderSettingsPatch;
	[[nodiscard]] bool HasHostViewParameterPatch(const LabParameterId& id,
		const ViewRenderSettingsPatch& patch) noexcept;
	// Defaults are inherited from the active content's resolved display view.
	[[nodiscard]] LabParameterSet DescribeHostViewParameters(
		const ResolvedViewRenderSettings& settings) noexcept;
	[[nodiscard]] std::optional<LabValue> ReadHostViewParameter(const LabParameterId& id,
		const ResolvedViewRenderSettings& settings) noexcept;
	// Only explicit overrides are applied. Exposure is resolved as one pair so its
	// scale and pre-exposure always agree with the values used by temporal planning.
	void ApplyHostViewParameters(std::span<const LabParameterValue> values,
		ResolvedViewRenderSettings& settings, bool enablePreExposure) noexcept;
}
