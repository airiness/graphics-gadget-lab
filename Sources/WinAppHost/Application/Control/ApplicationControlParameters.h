#pragma once
#include "Lab/LabParameter.h"
#include "Parameters/ParameterBatch.h"

#include <nlohmann/json_fwd.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace gglab
{
	class GGLabAppRuntime;
	struct RequiredFrameState;
	struct HostViewControlStamp;
	struct HostViewProjectionEntry;
	struct LightingProfilePublication;

	inline constexpr size_t ApplicationControlMaxParameterItems = 128;
	// Rejection codes of host-view-overrides; each is the leading word of its error text.
	inline constexpr std::array<std::string_view, 8> ApplicationControlHostViewOverrideErrorCodes =
	{
		"NoParameterSource", "Busy", "ExpectedRunChanged", "RunIdentityUnavailable", "ExpectedContentChanged",
		"ExpectedHostViewRevisionMismatch", "HostOverrideUnsupported", "HostOverrideInvalid",
	};

	struct ApplicationControlParameterChange
	{
		LabParameterType m_Type = LabParameterType::Bool;
		LabParameterValue m_Value;
	};

	[[nodiscard]] std::string ParseApplicationControlParameterChanges(
		const nlohmann::json& changes, std::vector<ApplicationControlParameterChange>& result);
	[[nodiscard]] std::string ParseApplicationControlParameterAssignments(
		const nlohmann::json& assignments, std::vector<LabParameterValue>& result);
	// Typed exact states: 'value' appears exactly when 'present' is true.
	[[nodiscard]] std::string ParseApplicationControlHostViewOverrides(
		const nlohmann::json& overrides, std::vector<HostViewOverrideState>& result);
	// Shape and names only; admission validates the values against the Host view schema.
	[[nodiscard]] std::string ParseApplicationControlRequiredFrameState(
		const nlohmann::json& document, std::shared_ptr<const RequiredFrameState>& result);
	[[nodiscard]] nlohmann::json SerializeApplicationControlParameterBatch(
		const ParameterBatchStatus& batch);
	[[nodiscard]] nlohmann::json SerializeApplicationControlLightingProfiles(
		const LightingProfilePublication* profiles);
	[[nodiscard]] nlohmann::json SerializeApplicationControlHostViewStamp(
		const HostViewControlStamp& stamp);
	[[nodiscard]] nlohmann::json SerializeApplicationControlDomainStamp(const ParameterControlStamp& stamp);
	// One object per Host view binding: final value, its source and override presence.
	[[nodiscard]] nlohmann::json SerializeApplicationControlHostViewProjection(
		std::span<const HostViewProjectionEntry> projection);
	[[nodiscard]] std::string SerializeApplicationControlParameters(
		uint64_t id, GGLabAppRuntime* runtime) noexcept;
	[[nodiscard]] std::string SubmitApplicationControlParameters(uint64_t id,
		std::span<const ApplicationControlParameterChange> changes, GGLabAppRuntime* runtime,
		const ParameterControlStamp* expected = nullptr) noexcept;
	// Rejections carry 'errorCode' and the live 'controlState' for recovery.
	[[nodiscard]] std::string SubmitApplicationControlHostViewOverrides(uint64_t id,
		const HostViewControlStamp& expected, std::span<const HostViewOverrideState> overrides,
		GGLabAppRuntime* runtime) noexcept;
	[[nodiscard]] std::string QueryApplicationControlParameters(
		uint64_t id, uint64_t batchId, bool consume, GGLabAppRuntime* runtime) noexcept;
}
