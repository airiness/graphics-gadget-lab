#pragma once
#include "Application/Lab/LabTypes.h"

#include <optional>

namespace gglab
{
	struct LabCommandBatch
	{
		std::optional<LabId> m_SwitchTarget;
		bool m_RestartRequested = false;
	};

	class LabCommandQueue
	{
	public:
		void RequestSwitch(const LabId& id) noexcept;
		void RequestRestart() noexcept;
		LabCommandBatch Consume() noexcept;
		bool IsEmpty() const noexcept;

	private:
		LabCommandBatch m_Pending;
	};
}
