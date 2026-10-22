#pragma once
#include "Lab/LabParameter.h"
#include "Parameters/ParameterBatch.h"

#include <nlohmann/json_fwd.hpp>

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace gglab
{
	class SessionParameterControl;

	inline constexpr size_t ApplicationControlMaxParameterItems = 128;

	struct ApplicationControlParameterChange
	{
		LabParameterType m_Type = LabParameterType::Bool;
		LabParameterValue m_Value;
	};

	[[nodiscard]] std::string ParseApplicationControlParameterChanges(
		const nlohmann::json& changes, std::vector<ApplicationControlParameterChange>& result);
	[[nodiscard]] nlohmann::json SerializeApplicationControlParameterBatch(
		const ParameterBatchStatus& batch);
	[[nodiscard]] std::string SerializeApplicationControlParameters(
		uint64_t id, SessionParameterControl* runtime) noexcept;
	[[nodiscard]] std::string SubmitApplicationControlParameters(uint64_t id,
		std::span<const ApplicationControlParameterChange> changes, SessionParameterControl* runtime,
		bool sequenceActive) noexcept;
	[[nodiscard]] std::string QueryApplicationControlParameters(
		uint64_t id, uint64_t batchId, bool consume, SessionParameterControl* runtime) noexcept;
}
