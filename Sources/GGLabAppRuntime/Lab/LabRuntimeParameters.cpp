#include "Lab/LabRuntime.h"
#include "GGLabFoundation/Base/CoreMacros.h"
#include "GGLabRuntime/Graphics/Camera.h"
#include "GGLabRuntime/Graphics/IBLBakeTypes.h"
#include "GGLabRuntime/Graphics/RenderSceneTypes.h"

#include <algorithm>
#include <limits>
#include <utility>

namespace gglab
{
	bool LabRuntime::SupportsParameterEffect(const LabParameterDesc& desc) const noexcept
	{
		const auto effect = GetParameterEffectClass(desc);
		return effect == ParameterEffectClass::Immediate ||
			(effect == ParameterEffectClass::Deferred && m_ActiveSession &&
				m_ActiveSession->GetLightingParameterRequest(desc.m_Id).has_value());
	}

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
		size_t lightingItems = 0;
		for (const LabParameterValue& value : values)
		{
			const auto& desc = candidate.Find(value.m_Id)->m_Desc;
			lightingItems += GetParameterEffectClass(desc) == ParameterEffectClass::Deferred;
			if (!SupportsParameterEffect(desc) || lightingItems > 1)
			{
				error = "Unsupported effect: a batch accepts Immediate items and at most one explicitly bound Deferred lighting item; rebuild effects remain unsupported.";
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
		// A failed item ends the complete wait. Close unresolved siblings while
		// preserving every stamp already reached; never leave pending items in a terminal batch.
		for (const auto& receipt : batch.m_Receipts)
		{
			if (receipt.GetSnapshot().m_State == ParameterEffectState::Failed)
			{
				batch.m_Status.m_Failure = receipt.GetSnapshot().m_Failure;
				break;
			}
		}
		const bool failed = !batch.m_Status.m_Failure.empty();
		batch.m_Status.m_Items.clear();
		bool terminal = !batch.m_Receipts.empty();
		for (auto& receipt : batch.m_Receipts)
		{
			if (failed && !receipt.GetSnapshot().IsTerminal())
			{
				(void)receipt.Fail("BatchEffectFailed: " + batch.m_Status.m_Failure);
			}
			batch.m_Status.m_Items.push_back(receipt.GetSnapshot());
			terminal &= receipt.GetSnapshot().IsTerminal();
		}
		if (terminal)
		{
			batch.m_Status.m_State = failed ? LabParameterBatchState::Failed : LabParameterBatchState::Finished;
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
				GetParameterEffectClass(m_ActiveSession->GetParameters().Find(value.m_Id)->m_Desc),
				m_ParameterRevision, serial);
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
			for (auto& receipt : batch.m_Receipts)
			{
				if (receipt.GetSnapshot().IsTerminal() ||
					receipt.GetSnapshot().m_DeclaredClass != ParameterEffectClass::Deferred)
				{
					continue;
				}
				const auto evidence = m_ActiveSession->GetLightingParameterRequest(receipt.GetSnapshot().m_Parameter);
				if (!evidence || evidence->m_PreviousGeneration != batch.m_FrameBakeRequestGeneration ||
					evidence->m_Generation != requested || requested == 0 ||
					requested - batch.m_FrameBakeRequestGeneration != 1)
				{
					(void)receipt.Fail("LightingEffectUnproven: the bound handler did not account for exactly one Runtime request.");
					continue;
				}
				batch.m_AttributedLightingRequest = receipt.RecordApplied(ParameterEffectClass::Deferred,
					{ batch.m_Status.m_Revision, "lighting", requested }, serial);
			}
		}
		RefreshParameterBatchStatus();
	}

	void LabRuntime::OnFrameBuilt(uint64_t serial, uint64_t lightingGeneration) noexcept
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
			if (receipt.GetSnapshot().m_DeclaredClass == ParameterEffectClass::Deferred)
			{
				const auto identity = receipt.GetSnapshot().m_Identity;
				if (lightingGeneration != 0 && lightingGeneration == identity.m_Generation)
				{
					// This generation came from the immutable built scene, not a later live query.
					(void)receipt.RecordPublished(identity, serial);
					batch.m_Consumed.push_back({ index, identity });
				}
				else if (bakeRequest != identity.m_Generation)
				{
					(void)receipt.Supersede();
				}
				else if (m_CreateInfo.m_Services.m_RenderServices.m_Environment->GetBakingStatus().m_Stage ==
					IBLBakeStage::Failed)
				{
					(void)receipt.Fail("LightingPublicationFailed");
				}
				continue;
			}
			if (batch.m_ApplyBakeGeneration != 0 && !batch.m_AttributedLightingRequest)
			{
				// Preserve actual application evidence even though it contradicts the
				// declaration. The owning Runtime supplied this request generation.
				(void)receipt.RecordApplied(ParameterEffectClass::Deferred,
					{ batch.m_Status.m_Revision, "lighting", batch.m_ApplyBakeGeneration }, serial);
				continue;
			}
			if (bakeRequest != batch.m_FrameBakeRequestGeneration &&
				(!batch.m_AttributedLightingRequest || bakeRequest != batch.m_ApplyBakeGeneration))
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
		if (feedback.m_RenderSceneStatus != RenderSceneBuildStatus::Ready)
		{
			m_ParameterBatch->m_Consumed.clear();
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
