#include "Lab/LabRuntime.h"
#include "GGLabFoundation/Base/CoreMacros.h"
#include "GGLabRuntime/Graphics/Camera.h"

#include <algorithm>
#include <limits>
#include <utility>

namespace gglab
{
	uint64_t LabRuntime::RequestParameterBatch(
		std::span<const LabParameterValue> values, std::string& error) noexcept
	{
		error.clear();
		if (m_ParameterBatch || !m_CommandQueue.IsEmpty())
		{
			error = "Busy: consume the previous batch result and drain pending Lab commands first.";
			return 0;
		}
		if (m_ParameterIntakeFailed || !m_IsEntered || !IsReady() || m_PendingSession)
		{
			error = "A parameter batch requires an active, ready Lab without a pending transition.";
			return 0;
		}
		if (m_NextParameterBatchId == 0 || m_ParameterRevision == std::numeric_limits<uint64_t>::max())
		{
			error = "Parameter transaction identity capacity is exhausted.";
			return 0;
		}
		// Preflight on an owned copy. Admission must not change live values/dirty bits.
		LabParameterSet candidate = m_ActiveSession->GetParameters();
		if (!candidate.ApplyBatch(values, error))
		{
			return 0;
		}
		for (const LabParameterValue& value : values)
		{
			if (GetParameterEffectClass(candidate.Find(value.m_Id)->m_Desc) !=
				ParameterEffectClass::Immediate)
			{
				error = "This Lab transaction path supports only Immediate effects; deferred/rebuild domain integration is pending.";
				return 0;
			}
		}
		const uint64_t id = m_NextParameterBatchId++;
		m_ParameterBatch.emplace();
		m_ParameterBatch->m_Status.m_BatchId = id;
		m_ParameterBatch->m_Status.m_Lab = m_ActiveSession->GetDescriptor().m_Id;
		m_ParameterBatch->m_Session = m_TemporalSessionSerial;
		m_ParameterBatch->m_Status.m_Values.assign(values.begin(), values.end());
		m_CommandQueue.RequestParameterBatch(id);
		return id;
	}

	const LabParameterBatchStatus* LabRuntime::GetParameterBatchStatus() const noexcept
	{
		return m_ParameterBatch ? &m_ParameterBatch->m_Status : nullptr;
	}

	std::optional<LabParameterBatchStatus> LabRuntime::TakeParameterBatchResult(uint64_t id) noexcept
	{
		if (!m_ParameterBatch || m_ParameterBatch->m_Status.m_BatchId != id ||
			!m_ParameterBatch->m_Status.IsTerminal())
		{
			return std::nullopt;
		}
		LabParameterBatchStatus result = std::move(m_ParameterBatch->m_Status);
		m_ParameterBatch.reset();
		return result;
	}

	void LabRuntime::RefreshParameterBatchStatus() noexcept
	{
		if (!m_ParameterBatch)
		{
			return;
		}
		auto& batch = *m_ParameterBatch;
		batch.m_Status.m_Items.clear();
		bool terminal = !batch.m_Receipts.empty();
		for (const auto& receipt : batch.m_Receipts)
		{
			batch.m_Status.m_Items.push_back(receipt.GetSnapshot());
			terminal &= receipt.GetSnapshot().IsTerminal();
			if (receipt.GetSnapshot().m_State == ParameterEffectState::Failed)
			{
				batch.m_Status.m_State = LabParameterBatchState::Failed;
				batch.m_Status.m_Failure = receipt.GetSnapshot().m_Failure;
			}
		}
		if (terminal && batch.m_Status.m_State != LabParameterBatchState::Failed)
		{
			batch.m_Status.m_State = LabParameterBatchState::Finished;
		}
	}

	void LabRuntime::EndParameterBatch(bool superseded, std::string reason) noexcept
	{
		(void)m_CommandQueue.ConsumeParameterBatch();
		if (!m_ParameterBatch || m_ParameterBatch->m_Status.IsTerminal())
		{
			return;
		}
		auto& batch = *m_ParameterBatch;
		if (batch.m_Receipts.empty())
		{
			batch.m_Status.m_State = LabParameterBatchState::Failed;
			batch.m_Status.m_Failure = std::move(reason);
			return;
		}
		for (auto& receipt : batch.m_Receipts)
		{
			if (superseded)
			{
				(void)receipt.Supersede();
			}
			else
			{
				(void)receipt.Finish();
			}
		}
		batch.m_Consumed.clear();
		RefreshParameterBatchStatus();
	}

	void LabRuntime::OnRuntimeFailed(std::string_view reason) noexcept
	{
		m_ParameterIntakeFailed = true;
		(void)m_CommandQueue.ConsumeParameterBatch();
		if (!m_ParameterBatch || m_ParameterBatch->m_Status.IsTerminal())
		{
			return;
		}
		for (auto& receipt : m_ParameterBatch->m_Receipts)
		{
			(void)receipt.Fail(std::string(reason));
		}
		m_ParameterBatch->m_Consumed.clear();
		RefreshParameterBatchStatus();
		m_ParameterBatch->m_Status.m_State = LabParameterBatchState::Failed;
		m_ParameterBatch->m_Status.m_Failure = reason;
	}

	void LabRuntime::OnFrameReady(uint64_t serial) noexcept
	{
		if (!m_ParameterBatch || m_ParameterBatch->m_Status.IsTerminal() || serial == 0)
		{
			return;
		}
		auto& batch = *m_ParameterBatch;
		batch.m_Consumed.clear();
		batch.m_FrameSerial = serial;
		if (!m_IsEntered || !m_ActiveSession ||
			batch.m_Session != m_TemporalSessionSerial)
		{
			EndParameterBatch(true, "The Lab session changed before the parameter frame.");
			return;
		}
		batch.m_FrameBakeRequestGeneration =
			m_CreateInfo.m_Services.m_RenderServices.m_Environment->GetBakeRequestGeneration();
		if (batch.m_Status.m_State != LabParameterBatchState::Queued)
		{
			return;
		}
		if (m_PendingSession)
		{
			EndParameterBatch(false, "The Lab started transitioning before parameter application.");
			return;
		}
		const auto command = m_CommandQueue.ConsumeParameterBatch();
		if (!command || *command != batch.m_Status.m_BatchId)
		{
			EndParameterBatch(false, "The pending parameter command lost its owner.");
			return;
		}
		std::string error;
		const auto applied = m_ActiveSession->SetParameters(batch.m_Status.m_Values, error);
		if (!applied)
		{
			batch.m_Status.m_State = LabParameterBatchState::Failed;
			batch.m_Status.m_Failure = std::move(error);
			return;
		}
		if (!applied->m_Changed.empty())
		{
			++m_ParameterRevision;
		}
		batch.m_Status.m_Revision = m_ParameterRevision;
		batch.m_Status.m_State = LabParameterBatchState::Applying;
		for (const auto& value : batch.m_Status.m_Values)
		{
			auto receipt = ParameterEffectReceipt::Begin(value.m_Id,
				ParameterEffectClass::Immediate, m_ParameterRevision, serial);
			GGLAB_ASSERT(receipt);
			if (std::ranges::find(applied->m_Changed, value.m_Id) == applied->m_Changed.end())
			{
				(void)receipt->RecordNoChange(serial);
			}
			batch.m_Receipts.push_back(std::move(*receipt));
		}
		if (!applied->m_Changed.empty())
		{
			m_ActiveSession->ApplyParameterChanges(LabChangeImpact::Immediate);
			m_ActiveSession->GetCamera().Update();
			const uint64_t requested =
				m_CreateInfo.m_Services.m_RenderServices.m_Environment->GetBakeRequestGeneration();
			batch.m_ApplyBakeGeneration = requested != batch.m_FrameBakeRequestGeneration ? requested : 0;
		}
		RefreshParameterBatchStatus();
	}

	void LabRuntime::OnFrameBuilt(uint64_t serial) noexcept
	{
		if (!m_ParameterBatch || m_ParameterBatch->m_Status.IsTerminal() ||
			serial != m_ParameterBatch->m_FrameSerial || !m_ActiveSession)
		{
			return;
		}
		auto& batch = *m_ParameterBatch;
		batch.m_Consumed.clear();
		const uint64_t bakeRequest =
			m_CreateInfo.m_Services.m_RenderServices.m_Environment->GetBakeRequestGeneration();
		for (size_t index = 0; index < batch.m_Receipts.size(); ++index)
		{
			auto& receipt = batch.m_Receipts[index];
			if (receipt.GetSnapshot().IsTerminal())
			{
				continue;
			}
			const auto& requested = batch.m_Status.m_Values[index];
			const auto* current = m_ActiveSession->GetParameters().Find(requested.m_Id);
			if (!current || !LabValuesEqual(current->m_Value, requested.m_Value))
			{
				(void)receipt.Supersede();
				continue;
			}
			if (batch.m_ApplyBakeGeneration != 0)
			{
				// Preserve actual application evidence even though it contradicts the
				// declaration. The owning Runtime supplied this request generation.
				(void)receipt.RecordApplied(ParameterEffectClass::Deferred,
					{ batch.m_Status.m_Revision, "lighting", batch.m_ApplyBakeGeneration }, serial);
				continue;
			}
			if (bakeRequest != batch.m_FrameBakeRequestGeneration)
			{
				// A later change has no proven attribution to the batch.
				(void)receipt.Fail("UnexpectedLightingTransition: effect attribution is unavailable for this frame.");
				continue;
			}
			const ParameterStateIdentity identity{ .m_Revision = batch.m_Status.m_Revision };
			if (receipt.RecordApplied(ParameterEffectClass::Immediate, identity, serial))
			{
				batch.m_Consumed.push_back({ index, identity });
			}
		}
		RefreshParameterBatchStatus();
	}

	void LabRuntime::ConsumeParameterFrame(const DemoFrameFeedback& feedback) noexcept
	{
		if (!m_ParameterBatch || m_ParameterBatch->m_Status.IsTerminal() ||
			feedback.m_RenderFrameSerial != m_ParameterBatch->m_FrameSerial ||
			!feedback.m_SubmittedFence.IsValid())
		{
			return;
		}
		for (const auto& consumed : m_ParameterBatch->m_Consumed)
		{
			(void)m_ParameterBatch->m_Receipts[consumed.m_Item].RecordSubmittedFrame(
				consumed.m_Identity, feedback.m_RenderFrameSerial);
		}
		m_ParameterBatch->m_Consumed.clear();
		RefreshParameterBatchStatus();
	}
}
