#pragma once
#include "Lab/LabParameter.h"
#include "Lab/LabRunConfig.h"
#include "Lab/LabTypes.h"

#include <optional>

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
		LabCommandBatch Consume() noexcept;
		bool IsEmpty() const noexcept;

	private:
		LabCommandBatch m_Pending;
	};
}
