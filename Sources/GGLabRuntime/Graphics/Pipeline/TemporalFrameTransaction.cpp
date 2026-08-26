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

	void TemporalObjectHistory::Invalidate() noexcept
	{
		m_Committed.clear();
		m_LastCommittedFrame = 0;
	}

	const TemporalCommittedObjectState* TemporalObjectHistory::Find(
		const RenderObjectHistoryKey& key) const noexcept
	{
		const auto iterator = m_Committed.find(key);
		return iterator != m_Committed.end() ? &iterator->second : nullptr;
	}

	TemporalObjectHistoryDiagnostics TemporalObjectHistory::GetDiagnostics() const noexcept
	{
		return {
			.m_EntryCount = static_cast<uint32_t>(m_Committed.size()),
			.m_Capacity = MaxObjectCapacity,
			.m_LastCommittedFrame = m_LastCommittedFrame,
		};
	}

	void TemporalFrameTransaction::Begin(TemporalViewHistory& viewHistory,
		TemporalObjectHistory& objectHistory, const ResolvedTemporalFramePlan& plan,
		uint32_t width, uint32_t height) noexcept
	{
		GGLAB_ASSERT_MSG(m_State != TemporalFrameTransactionState::Pending,
			"A pending temporal frame transaction must be ended before "
			"it can be reused.");
		m_ViewHistory = &viewHistory;
		m_ObjectHistory = &objectHistory;
		m_Plan = plan;
		m_PendingView = {};
		m_State = TemporalFrameTransactionState::Pending;
		m_Width = width;
		m_Height = height;
		m_HasCompatiblePreviousView = plan.m_Active && IsCompatible(viewHistory);
		m_JitterIndex = m_HasCompatiblePreviousView ? viewHistory.m_NextJitterIndex : 0;
		m_JitterPixels =
			plan.m_Active ? temporal::GetJitterSamplePixels(m_JitterIndex) : Vector2::Zero;
		m_HasPendingView = false;
		m_PendingObjects.clear();
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
			const TemporalCommittedViewState& previous = m_ViewHistory->m_Committed;
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

	Matrix TemporalFrameTransaction::ResolvePreviousObjectModel(
		const RenderObjectHistoryKey& key, const Matrix& currentModel) const noexcept
	{
		GGLAB_ASSERT_MSG(m_State == TemporalFrameTransactionState::Pending,
			"Object history lookup requires a pending temporal frame transaction.");
		if (!m_HasCompatiblePreviousView || !key.IsValid() ||
			key.m_SessionIdentity != m_Plan.m_SessionIdentity || !m_ObjectHistory)
		{
			return currentModel;
		}
		const TemporalCommittedObjectState* previous = m_ObjectHistory->Find(key);
		return previous ? previous->m_Model : currentModel;
	}

	bool TemporalFrameTransaction::StageSubmittedObject(
		const RenderObjectHistoryKey& key, const Matrix& currentModel) noexcept
	{
		GGLAB_ASSERT_MSG(m_State == TemporalFrameTransactionState::Pending,
			"Object history staging requires a pending temporal frame transaction.");
		if (!m_Plan.m_Active)
		{
			return true;
		}
		if (!key.IsValid() || key.m_SessionIdentity != m_Plan.m_SessionIdentity)
		{
			return false;
		}
		const auto pending = m_PendingObjects.find(key);
		if (pending != m_PendingObjects.end())
		{
			pending->second = currentModel;
			return true;
		}
		if (m_PendingObjects.size() >= MaxObjectCapacity)
		{
			return false;
		}
		m_PendingObjects.emplace(key, currentModel);
		return true;
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
		GGLAB_ASSERT_NOT_NULL(m_ViewHistory);
		GGLAB_ASSERT_NOT_NULL(m_ObjectHistory);
		if (!m_Plan.m_Active)
		{
			m_ViewHistory->Invalidate();
			m_ObjectHistory->Invalidate();
			m_State = TemporalFrameTransactionState::Committed;
			return;
		}
		if (!m_HasPendingView || !m_ParticipatedInResolve)
		{
			m_State = TemporalFrameTransactionState::Aborted;
			return;
		}

		m_ViewHistory->m_Committed = m_PendingView;
		m_ViewHistory->m_NextJitterIndex =
			(m_JitterIndex + 1) % temporal::JitterSampleCount;
		m_ViewHistory->m_Valid = true;
		CommitObjectHistory();
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
		if (m_State == TemporalFrameTransactionState::Pending && m_ViewHistory && m_ObjectHistory)
		{
			m_ViewHistory->Invalidate();
			m_ObjectHistory->Invalidate();
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

	void TemporalFrameTransaction::CommitObjectHistory() noexcept
	{
		GGLAB_ASSERT_NOT_NULL(m_ObjectHistory);
		const uint64_t committedFrame = ++m_ObjectHistory->m_LastCommittedFrame;
		GGLAB_ASSERT_MSG(committedFrame != 0,
			"Temporal object history committed-frame serial overflowed its valid range.");
		for (const auto& [key, model] : m_PendingObjects)
		{
			m_ObjectHistory->m_Committed[key] = {
				.m_Model = model,
				.m_LastSeenCommittedFrame = committedFrame,
			};
		}
		for (auto iterator = m_ObjectHistory->m_Committed.begin();
			iterator != m_ObjectHistory->m_Committed.end();)
		{
			if (iterator->second.m_LastSeenCommittedFrame != committedFrame)
			{
				iterator = m_ObjectHistory->m_Committed.erase(iterator);
			}
			else
			{
				++iterator;
			}
		}
		GGLAB_ASSERT_MSG(m_ObjectHistory->m_Committed.size() <= MaxObjectCapacity,
			"Temporal object history exceeded the bounded GPU object capacity.");
	}
}
