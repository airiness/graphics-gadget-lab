#include "Capture/GIProbeInspectionControl.h"
#include "GGLabAppRuntime.h"
#include "Capture/CapturePlan.h"
#include "Capture/CaptureJobTypes.h"

#include <utility>

namespace gglab
{
	bool ToolingGIProbeInspection::Inspect(GIProbeInspectionRequest request) noexcept
	{
		Update();
		if (m_JobId != 0)
		{
			return false;
		}
		CapturePlan plan;
		plan.m_Label = "probe-inspection";
		plan.m_RequiredProducts.push_back(ResourceCaptureProduct{
			{ { .m_Resource = "GI.Field.ProbeInspection" } } });
		plan.m_GIProbeInspection = request;
		const auto admission = m_Runtime.TrySubmitCaptureJob(std::move(plan));
		if (admission.m_Status != CaptureAdmissionStatus::Accepted)
		{
			m_Status = "Probe inspection admission rejected or busy.";
			return false;
		}
		m_JobId = admission.m_JobId;
		m_Discard = false;
		m_Result.reset();
		m_Status = "Pending GPU completion and capture publication.";
		return true;
	}

	void ToolingGIProbeInspection::Cancel() noexcept
	{
		if (m_JobId != 0)
		{
			GGLAB_UNUSED(m_Runtime.CancelCaptureJob(m_JobId));
			m_Discard = true;
			m_Status = "Selection superseded; draining owned job.";
		}
		else
		{
			m_Status = "No probe inspection requested.";
		}
		m_Result.reset();
	}

	void ToolingGIProbeInspection::Update() noexcept
	{
		if (m_JobId == 0)
		{
			return;
		}
		const auto result = m_Runtime.TakeCaptureJobResult(m_JobId);
		if (!result)
		{
			return;
		}
		m_JobId = 0;
		if (m_Discard)
		{
			m_Status = "Superseded result discarded.";
			return;
		}
		m_Status = "Inspection failed or cancelled.";
		if (result->m_Status == CaptureJobStatus::Completed &&
			result->m_Publication == CaptureJobPublication::Published)
		{
			for (const auto& product : result->m_Products)
			{
				if (product.m_Metadata && product.m_Metadata->m_GIProbeInspection)
				{
					m_Result = product.m_Metadata->m_GIProbeInspection;
					m_Status = "Completed inspection (historical GPU observation).";
				}
			}
		}
		else if (!result->m_FailureCode.empty())
		{
			m_Status = result->m_FailureCode;
		}
	}
}
