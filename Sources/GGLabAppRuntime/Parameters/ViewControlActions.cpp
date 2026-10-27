#include "Parameters/ViewControlActions.h"
#include "GGLabAppRuntime.h"
#include "Parameters/SessionParameterControl.h"
#include "Capture/FrameSequenceCoordinator.h"

#include <utility>

namespace gglab
{
	namespace
	{
		constexpr auto ToolingOwner = ParameterBatchOwner::Tooling(1);
	}

	bool ToolingViewControl::ReleaseOverride(const HostViewControlStamp& expected,
		const LabParameterId& parameter, LabParameterType type) noexcept
	{
		m_Runtime.CheckOwnerThread();
		Update();
		if (m_Status.IsPending())
		{
			return false;
		}
		m_Status = { .m_Parameter = parameter, .m_Expected = expected };
		ParameterRequest request{
			.m_Entries = { { parameter, type, ParameterOperation::RemoveOverride, std::nullopt } },
			.m_Expected = expected,
		};
		const auto id = m_Runtime.RequestParametersForOwner(request, m_Status.m_Error, ToolingOwner);
		if (!id)
		{
			if (m_Status.m_Error.starts_with("Busy:"))
			{
				if (const auto* sequence = m_Runtime.GetFrameSequenceCoordinator(); sequence && sequence->IsActive())
				{
					m_Status.m_BlockingOwner = ParameterBatchOwner::Sequence(sequence->GetStatus()->m_SequenceId);
				}
				else if (const auto* control = m_Runtime.GetParameterControl())
				{
					m_Status.m_BlockingOwner = control->GetRetainedParameterOwner();
				}
			}
			return false;
		}
		m_Status.m_Batch = *m_Runtime.m_ParameterControl->GetParameterBatchStatus(id, ToolingOwner);
		return true;
	}

	void ToolingViewControl::Update() noexcept
	{
		if (!m_Status.IsPending())
		{
			return;
		}
		auto& control = *m_Runtime.m_ParameterControl;
		const auto id = m_Status.m_Batch->m_BatchId;
		const auto* status = control.GetParameterBatchStatus(id, ToolingOwner);
		if (!status)
		{
			m_Status.m_Error = "ParameterResultUnavailable: the tooling release result was lost.";
			m_Status.m_Batch.reset();
			return;
		}
		if (status->IsTerminal())
		{
			if (auto result = control.TakeParameterBatchResult(id, ToolingOwner))
			{
				m_Status.m_Batch = std::move(result);
			}
		}
		else
		{
			m_Status.m_Batch = *status;
		}
	}
}
