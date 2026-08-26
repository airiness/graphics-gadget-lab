#include "Graphics/Pipeline/TemporalFrameTransaction.h"
#include "Core/Math/MathFunctions.h"
#include "GGLabFoundation/Base/CoreMacros.h"
#include "Graphics/RenderView.h"
#include "Graphics/ScreenSpace/ScreenSpaceTypes.h"

namespace gglab
{
	void TemporalViewHistory::Invalidate() noexcept
	{
		m_Committed = {};
		m_NextJitterIndex = 0;
		m_Valid = false;
	}

	void TemporalFrameTransaction::Begin(TemporalViewHistory& history,
		const ResolvedTemporalFramePlan& plan, uint32_t width, uint32_t height) noexcept
	{
		GGLAB_ASSERT_MSG(m_State != TemporalFrameTransactionState::Pending,
			"A pending temporal frame transaction must be ended before "
			"it can be reused.");
		m_History = &history;
		m_Plan = plan;
		m_PendingView = {};
		m_State = TemporalFrameTransactionState::Pending;
		m_Width = width;
		m_Height = height;
		m_HasCompatiblePreviousView = plan.m_Active && IsCompatible(history);
		m_JitterIndex = m_HasCompatiblePreviousView ? history.m_NextJitterIndex : 0;
		m_JitterPixels =
			plan.m_Active ? temporal::GetJitterSamplePixels(m_JitterIndex) : Vector2::Zero;
		m_HasPendingView = false;
		m_ParticipatedInResolve = false;
	}

	void TemporalFrameTransaction::PrepareDisplayView(RenderView& view) noexcept
	{
		GGLAB_ASSERT_MSG(m_State == TemporalFrameTransactionState::Pending,
			"Display view preparation requires a pending temporal frame "
			"transaction.");
		GGLAB_ASSERT_MSG(view.m_IsValid && view.m_ViewId == m_Plan.m_DisplayViewId,
			"Temporal frame transaction can only prepare its resolved display view.");

		view.m_TemporalResetIdentity = m_Plan.m_ResetIdentity;
		view.m_TemporalSessionIdentity = m_Plan.m_SessionIdentity;
		if (m_Plan.m_Active)
		{
			const Vector2 jitterNDC =
				temporal::JitterPixelsToNDC(m_JitterPixels, m_Width, m_Height);
			Matrix clipJitter = Matrix::Identity;
			clipJitter.m_41 = jitterNDC.m_X;
			clipJitter.m_42 = jitterNDC.m_Y;
			view.m_RasterProj = view.m_UnjitteredProj * clipJitter;
			view.m_RasterViewProj = view.m_View * view.m_RasterProj;
			view.m_InvRasterProj = math::Inverse(view.m_RasterProj);
			view.m_InvRasterViewProj = math::Inverse(view.m_RasterViewProj);
			view.m_DepthReconstructionParams =
				screen_space::MakeDepthReconstructionParams(view.m_RasterProj);
			view.m_JitterPixels = m_JitterPixels;
			view.m_JitterUV = temporal::JitterPixelsToUV(m_JitterPixels, m_Width, m_Height);
		}

		if (m_HasCompatiblePreviousView)
		{
			const TemporalCommittedViewState& previous = m_History->m_Committed;
			view.m_PreviousView = previous.m_View;
			view.m_PreviousRasterViewProj = previous.m_RasterViewProj;
			view.m_PreviousDepthReconstructionParams = previous.m_DepthReconstructionParams;
			view.m_PreviousJitterUV = previous.m_JitterUV;
			view.m_PreviousDepthConvention = previous.m_DepthConvention;
			view.m_HasPreviousTemporalState = true;
		}
		else
		{
			view.m_PreviousView = view.m_View;
			view.m_PreviousRasterViewProj = view.m_RasterViewProj;
			view.m_PreviousDepthReconstructionParams = view.m_DepthReconstructionParams;
			view.m_PreviousJitterUV = view.m_JitterUV;
			view.m_PreviousDepthConvention = view.m_DepthConvention;
			view.m_HasPreviousTemporalState = false;
		}

		m_PendingView = {
			.m_View = view.m_View,
			.m_RasterViewProj = view.m_RasterViewProj,
			.m_DepthReconstructionParams = view.m_DepthReconstructionParams,
			.m_JitterUV = view.m_JitterUV,
			.m_DepthConvention = view.m_DepthConvention,
			.m_DisplayViewId = view.m_ViewId,
			.m_ResetIdentity = m_Plan.m_ResetIdentity,
			.m_SessionIdentity = m_Plan.m_SessionIdentity,
			.m_Width = m_Width,
			.m_Height = m_Height,
		};
		m_HasPendingView = true;
	}

	void TemporalFrameTransaction::MarkResolveParticipated() noexcept
	{
		GGLAB_ASSERT_MSG(m_State == TemporalFrameTransactionState::Pending && m_Plan.m_Active,
			"Only an active pending temporal frame may participate in resolve.");
		m_ParticipatedInResolve = true;
	}

	void TemporalFrameTransaction::CommitCompleted() noexcept
	{
		if (m_State != TemporalFrameTransactionState::Pending)
		{
			return;
		}
		GGLAB_ASSERT_NOT_NULL(m_History);
		if (!m_Plan.m_Active)
		{
			m_History->Invalidate();
			m_State = TemporalFrameTransactionState::Committed;
			return;
		}
		if (!m_HasPendingView || !m_ParticipatedInResolve)
		{
			m_State = TemporalFrameTransactionState::Aborted;
			return;
		}

		m_History->m_Committed = m_PendingView;
		m_History->m_NextJitterIndex = (m_JitterIndex + 1) % temporal::JitterSampleCount;
		m_History->m_Valid = true;
		m_State = TemporalFrameTransactionState::Committed;
	}

	void TemporalFrameTransaction::Abort() noexcept
	{
		if (m_State == TemporalFrameTransactionState::Pending)
		{
			m_State = TemporalFrameTransactionState::Aborted;
		}
	}

	void TemporalFrameTransaction::InvalidateAfterFatal() noexcept
	{
		if (m_State == TemporalFrameTransactionState::Pending && m_History)
		{
			m_History->Invalidate();
			m_State = TemporalFrameTransactionState::Invalidated;
		}
	}

	bool TemporalFrameTransaction::IsCompatible(const TemporalViewHistory& history) const noexcept
	{
		const TemporalCommittedViewState& committed = history.m_Committed;
		return history.m_Valid && committed.m_DisplayViewId == m_Plan.m_DisplayViewId &&
			   committed.m_ResetIdentity == m_Plan.m_ResetIdentity &&
			   committed.m_SessionIdentity == m_Plan.m_SessionIdentity &&
			   committed.m_Width == m_Width && committed.m_Height == m_Height;
	}
}
