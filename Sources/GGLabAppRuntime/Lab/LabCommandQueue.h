#pragma once
#include "Lab/LabParameter.h"
#include "Lab/LabRunConfig.h"
#include "Lab/LabTypes.h"

#include <cstdint>
#include <optional>
#include <vector>

namespace gglab
{
	struct LabCommandBatch
	{
		std::optional<LabId> m_SwitchTarget;
		std::optional<LabRunConfig> m_RunConfig;
		std::vector<LabParameterValue> m_ParameterChanges;
		bool m_RestartRequested = false;
		bool m_ResetParametersRequested = false;
		bool m_RebuildSceneRequested = false;
	};

	class LabCommandQueue
	{
	public:
		void RequestSwitch(const LabId& id) noexcept;
		void RequestSetParameter(const LabParameterId& id, const LabValue& value) noexcept;
		void RequestRestart() noexcept;
		void RequestResetParameters() noexcept;
		void RequestRebuildScene() noexcept;
		void RequestRunConfig(const LabRunConfig& config) noexcept;
		// Strict transactions share the safe command owner, but survive the UI's
		// Update-time consume until a real Ready render frame supplies its serial.
		void RequestParameterBatch(uint64_t id) noexcept;
		void CancelParameterBatch(uint64_t id) noexcept;
		[[nodiscard]] bool HasParameterBatch(uint64_t id) const noexcept { return m_ParameterBatch == id; }
		[[nodiscard]] std::optional<uint64_t> ConsumeParameterBatch() noexcept;
		[[nodiscard]] bool ConsumeParameterBatch(uint64_t id) noexcept;
		LabCommandBatch Consume() noexcept;
		bool IsEmpty() const noexcept;

	private:
		LabCommandBatch m_Pending;
		std::optional<uint64_t> m_ParameterBatch;
	};
}
