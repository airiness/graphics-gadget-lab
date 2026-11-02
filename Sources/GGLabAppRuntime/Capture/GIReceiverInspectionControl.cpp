#include "Capture/GIReceiverInspectionControl.h"
#include "Capture/CaptureJobTypes.h"
#include "Capture/CapturePlan.h"
#include "GGLabAppRuntime.h"
#include "GGLabFoundation/Base/CoreMacros.h"

#include <utility>

namespace gglab
{
	bool ToolingGIReceiverInspection::Inspect(GIReceiverInspectionRequest request) noexcept
	{
		Update();
		if (m_JobId != 0)
		{
			return false;
		}
		CapturePlan plan;
		plan.m_Label = "receiver-inspection";
		plan.m_RequiredProducts = { ImageCaptureProduct{ CaptureImageSource::Composited },
			ResourceCaptureProduct{ { { .m_Resource = "GI.Field.ReceiverInspection" } } } };
		plan.m_GIReceiverInspection = request;
		const auto admission = m_Runtime.TrySubmitCaptureJob(std::move(plan));
		if (admission.m_Status != CaptureAdmissionStatus::Accepted)
		{
			m_Status = "Receiver inspection admission rejected or busy.";
			return false;
		}
		m_JobId = admission.m_JobId;
		m_Discard = false;
		m_Result.reset();
		m_ImagePath.clear();
		m_Status = "Pending GPU query, paired marker image and publication.";
		return true;
	}

	void ToolingGIReceiverInspection::Cancel() noexcept
	{
		if (m_JobId != 0)
		{
			GGLAB_UNUSED(m_Runtime.CancelCaptureJob(m_JobId));
			m_Discard = true;
			m_Status = "Selection superseded; draining owned job.";
		}
		else
		{
			m_Status = "No receiver inspection requested.";
		}
		m_Result.reset();
		m_ImagePath.clear();
	}

	void ToolingGIReceiverInspection::Update() noexcept
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
		m_Status = result->m_FailureCode.empty() ? "Inspection failed or cancelled." : result->m_FailureCode;
		if (result->m_Status != CaptureJobStatus::Completed || result->m_Publication != CaptureJobPublication::Published)
		{
			return;
		}
		for (const auto& product : result->m_Products)
		{
			if (product.m_Metadata && product.m_Metadata->m_GIReceiverInspection)
			{
				m_Result = product.m_Metadata->m_GIReceiverInspection;
				m_Status = "Completed receiver observation and paired marker capture.";
			}
			for (const auto& file : product.m_Files)
			{
				if (file.extension() == ".png")
				{
					m_ImagePath = GetFrameCapturePathText(file);
				}
			}
		}
	}
}
