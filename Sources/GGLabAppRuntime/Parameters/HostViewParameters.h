#pragma once
#include "Lab/LabParameter.h"
#include "ApplicationToolingIntegration.h"
#include "GGLabRuntime/Graphics/ViewRenderSettings.h"

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

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

	// Identifies the Host view override state of one process run and content activation.
	struct HostViewControlStamp
	{
		std::string m_RunIdentity;
		uint64_t m_ContentIdentity = 0;
		uint64_t m_HostViewRevision = 0;
	};

	// The resolver layer that supplied a display-view value, in application order.
	enum class HostViewValueSource : uint8_t
	{
		Content,
		Tooling,
		Camera,
		Host,
		Sequence,
		ForcedReference,
	};

	[[nodiscard]] std::string_view GetHostViewValueSourceName(HostViewValueSource source) noexcept;

	struct HostViewProjectionEntry
	{
		LabParameterId m_Id;
		LabParameterType m_Type = LabParameterType::Bool;
		LabValue m_Value = false;
		HostViewValueSource m_Source = HostViewValueSource::Content;
		HostViewValueSource m_InheritedSource = HostViewValueSource::Content;
		// Engaged exactly when a Host view override is present.
		std::optional<LabValue> m_Override;
	};

	// Frame-local patches applied after Host overrides. A frame applies at most one.
	struct HostViewFramePatches
	{
		const ViewRenderSettingsPatch* m_Sequence = nullptr;
		const ViewRenderSettingsPatch* m_ForcedReference = nullptr;
	};

	// Attributes each Host view binding to the last layer that wrote it; numeric
	// equality between layers never changes the source.
	[[nodiscard]] std::vector<HostViewProjectionEntry> DescribeHostViewProjection(
		const ResolvedViewRenderSettings& settings, const ApplicationToolingBlocks& tooling,
		std::span<const LabParameterValue> overrides, const HostViewFramePatches& patches = {});
}
