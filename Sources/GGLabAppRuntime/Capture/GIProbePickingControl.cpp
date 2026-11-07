#include "Capture/GIProbePickingControl.h"
#include "Capture/CaptureJobTypes.h"
#include "Capture/CapturePlan.h"
#include "GGLabAppRuntime.h"
#include "GGLabFoundation/Base/CoreMacros.h"

#include <utility>

namespace gglab
{
	void ToolingGIProbePicking::OnPresented(std::shared_ptr<const GIFieldSummary> summary,
		float dpiScale, std::string contentId) noexcept
	{
		m_Display = { std::move(summary), dpiScale, std::move(contentId) };
	}

	bool ToolingGIProbePicking::Pick(GIProbePickingRequest request) noexcept
	{
		if (!IsGIProbePickingRequestValid(request) || !m_Display.m_Summary || m_Display.m_ContentId.empty())
		{
			m_Status = "Picking source unavailable or malformed.";
			return false;
		}
		if (m_JobId)
		{
			GGLAB_UNUSED(m_Runtime.CancelCaptureJob(m_JobId));
			m_Discard = true;
		}
		m_Latest = std::move(request);
		m_SourceContent = m_Display.m_ContentId;
		m_Result.reset();
		m_Status = "Latest click queued; earlier submitted work drains independently.";
		SubmitLatest();
		return true;
	}

	void ToolingGIProbePicking::SubmitLatest() noexcept
	{
		if (m_JobId || !m_Latest)
		{
			return;
		}
		CapturePlan plan;
		plan.m_Label = "probe-picking";
		plan.m_Timing = FrameCaptureTiming::NextFrame;
		plan.m_SettleFrames = 0;
		plan.m_RequiredContentId = m_SourceContent;
		plan.m_GIProbePicking = m_Latest;
		plan.m_RequiredProducts = { ResourceCaptureProduct{ {
			{ .m_Resource = "GI.Probes.PickID" }, { .m_Resource = "GI.Probes.PickDepth" } } } };
		const auto admission = m_Runtime.TrySubmitCaptureJob(std::move(plan));
		if (admission.m_Status == CaptureAdmissionStatus::Busy)
		{
			return;
		}
		if (admission.m_Status != CaptureAdmissionStatus::Accepted)
		{
			m_Status = "Probe picking admission rejected.";
			m_Latest.reset();
			return;
		}
		m_JobId = admission.m_JobId;
		m_Latest.reset();
		m_Discard = false;
		m_Status = "Pending bounded GPU picking and publication.";
	}

	void ToolingGIProbePicking::Cancel() noexcept
	{
		m_Latest.reset();
		m_Result.reset();
		if (m_JobId)
		{
			GGLAB_UNUSED(m_Runtime.CancelCaptureJob(m_JobId));
			m_Discard = true;
		}
		m_Status = "GUI pick superseded; submitted storage drains through capture.";
	}

	void ToolingGIProbePicking::Update() noexcept
	{
		if (m_JobId)
		{
			const auto result = m_Runtime.TakeCaptureJobResult(m_JobId);
			if (!result)
			{
				return;
			}
			m_JobId = 0;
			if (!m_Discard)
			{
				m_Status = result->m_FailureCode.empty() ? "Picking failed or cancelled." : result->m_FailureCode;
				if (result->m_Status == CaptureJobStatus::Completed && result->m_Publication == CaptureJobPublication::Published)
				{
					for (const auto& product : result->m_Products)
					{
						if (product.m_Metadata && product.m_Metadata->m_GIProbePicking)
						{
							m_Result = product.m_Metadata->m_GIProbePicking;
							m_Status = m_Result->m_Validity == GIProbePickingValidity::Hit ? "Hit" :
								m_Result->m_Validity == GIProbePickingValidity::NoHit ? "NoHit" : "MalformedPayload";
						}
					}
				}
			}
		}
		SubmitLatest();
	}
}
