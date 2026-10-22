#include "Parameters/SessionParameterControl.h"
#include "Parameters/HostViewParameters.h"
#include "Demo/DemoBase.h"
#include "Lab/LabRuntime.h"
#include "GGLabFoundation/Base/CoreMacros.h"
#include "GGLabRuntime/Graphics/CameraRig.h"
#include "GGLabRuntime/Graphics/RenderSceneTypes.h"
#include "GGLabRuntime/Graphics/RenderServices.h"

#include <algorithm>
#include <limits>
#include <utility>
#include <string>
#include <vector>

namespace gglab
{
	SessionParameterControl::SessionParameterControl(RenderEnvironmentAccess& environment) noexcept :
		m_Environment(environment)
	{
	}

	void SessionParameterControl::SynchronizeContent(DemoBase* demo, uint32_t demoSerial,
		std::string_view demoId, bool transitioning) noexcept
	{
		const auto* lab = demo ? demo->GetParameterLabRuntime() : nullptr;
		const uint32_t contentSerial = lab ?
			lab->GetTemporalSessionSerial() :
			demo ? demo->GetTemporalSessionSerial() : 0;
		const uint64_t serial = demo ? (static_cast<uint64_t>(demoSerial) << 32) | contentSerial : 0;
		if (demo != m_Demo || serial != m_ContentSerial)
		{
			// The previous Lab may already be retired. Never dereference its borrowed pointer.
			m_Lab = nullptr;
			EndParameterBatch(true, "The content changed before the parameter frame.");
			m_HostOverrides.clear();
			m_HostParameters = {};
			if (m_Enabled && m_Demo && m_ParameterRevision != std::numeric_limits<uint64_t>::max())
			{
				++m_ParameterRevision;
			}
			m_ContentSerial = serial;
			m_ObservedLabRevision = 0;
		}
		m_Demo = demo;
		m_Lab = demo ? demo->GetParameterLabRuntime() : nullptr;
		m_ContentId = demo ? demo->GetCaptureContentId() : std::string{};
		if (m_ContentId.empty() && m_Lab && m_Lab->GetActiveSession())
		{
			m_ContentId = m_Lab->GetActiveSession()->GetDescriptor().m_Id.GetName();
		}
		if (m_ContentId.empty() && demo)
		{
			m_ContentId = demoId;
		}
		m_Transitioning = transitioning;
		if (m_Enabled)
		{
			RefreshSources();
		}
	}

	void SessionParameterControl::Enable() noexcept
	{
		m_Enabled = true;
		RefreshSources();
	}

	bool SessionParameterControl::IsAvailable() const noexcept
	{
		if (!m_Demo || m_Transitioning || m_ParameterIntakeFailed ||
			(m_Lab && (!m_Lab->IsReady() || m_Lab->HasPendingSession())))
		{
			return false;
		}
		const auto progress = m_Demo->GetActiveLoadingProgress();
		return !progress || progress->IsReady();
	}

	const LabParameterSet* SessionParameterControl::GetLabParameters() const noexcept
	{
		const auto* session = m_Lab ? m_Lab->GetActiveSession() : nullptr;
		return session ? &session->GetParameters() : nullptr;
	}

	const LabParameter* SessionParameterControl::Find(const LabParameterId& id) const noexcept
	{
		if (const auto* host = m_HostParameters.Find(id))
		{
			return host;
		}
		const auto* lab = GetLabParameters();
		return lab ? lab->Find(id) : nullptr;
	}

	bool SessionParameterControl::SupportsParameterEffect(const LabParameterDesc& desc) const noexcept
	{
		return m_HostParameters.Find(desc.m_Id) || (m_Lab && m_Lab->SupportsParameterEffect(desc));
	}

	void SessionParameterControl::RefreshSources() noexcept
	{
		if (!m_Demo || (m_Lab && !m_Lab->GetActiveSession()))
		{
			return;
		}
		const auto view = m_Demo->GetCameraRig().ResolveEffectiveDisplayView();
		if (!view.IsValid())
		{
			return;
		}
		auto host = DescribeHostViewParameters(ResolveViewRenderSettings(
			m_Demo->GetViewRenderProfile(), *view.m_CameraSlot->m_Camera));
		std::string error;
		if (!m_HostOverrides.empty())
		{
			GGLAB_UNUSED(host.ApplyBatch(m_HostOverrides, error));
		}
		const auto before = m_HostParameters.CaptureValues();
		const auto after = host.CaptureValues();
		const bool hostChanged = !before.empty() && !std::ranges::equal(before, after,
			[](const auto& a, const auto& b) { return a.m_Id == b.m_Id && LabValuesEqual(a.m_Value, b.m_Value); });
		const uint64_t labRevision = m_Lab ? m_Lab->GetParameterRevision() : 0;
		if ((hostChanged || (m_ObservedLabRevision != 0 && labRevision != m_ObservedLabRevision)) &&
			m_ParameterRevision != std::numeric_limits<uint64_t>::max())
		{
			++m_ParameterRevision;
		}
		m_HostParameters = std::move(host);
		m_ObservedLabRevision = labRevision;
	}

	uint64_t SessionParameterControl::RequestParameterBatch(
		std::span<const LabParameterValue> values, std::string& error) noexcept
	{
		Enable();
		error.clear();
		if (m_ParameterBatch)
		{
			error = "Busy: consume the previous parameter batch result first.";
			return 0;
		}
		if (!IsAvailable() || values.empty() || values.size() > SessionParameterMaxItems)
		{
			error = "A parameter batch requires ready active content and 1-128 items.";
			return 0;
		}
		if (m_NextParameterBatchId == 0 || m_ParameterRevision == std::numeric_limits<uint64_t>::max())
		{
			error = "Parameter transaction identity capacity is exhausted.";
			return 0;
		}
		LabParameterSet candidate = m_HostParameters;
		if (const auto* lab = GetLabParameters())
		{
			for (const auto& parameter : lab->GetParameters())
			{
				if (!candidate.Add(parameter.m_Desc))
				{
					error = "Host and Lab parameter ids must be distinct.";
					return 0;
				}
			}
		}
		if (!candidate.ApplyBatch(values, error))
		{
			return 0;
		}
		std::vector<LabParameterValue> labValues;
		bool hasHost = false;
		for (const auto& value : values)
		{
			if (m_HostParameters.Find(value.m_Id))
			{
				hasHost = true;
			}
			else
			{
				labValues.push_back(value);
			}
		}
		const uint64_t id = m_NextParameterBatchId;
		if (!labValues.empty() && !m_Lab->QueueParameterBatch(id, labValues, error))
		{
			return 0;
		}
		++m_NextParameterBatchId;
		m_ParameterBatch.emplace();
		auto& batch = *m_ParameterBatch;
		batch.m_Status.m_BatchId = id;
		batch.m_Status.m_ContentId = m_ContentId;
		batch.m_Status.m_Source = hasHost ? (labValues.empty() ? "host" : "mixed") : "lab";
		batch.m_Session = m_ContentSerial;
		batch.m_Status.m_Values.assign(values.begin(), values.end());
		batch.m_LabValues = std::move(labValues);
		return id;
	}

	const ParameterBatchStatus* SessionParameterControl::GetParameterBatchStatus() const noexcept
	{
		return m_ParameterBatch ? &m_ParameterBatch->m_Status : nullptr;
	}

	std::optional<ParameterBatchStatus> SessionParameterControl::TakeParameterBatchResult(uint64_t id) noexcept
	{
		if (!m_ParameterBatch || m_ParameterBatch->m_Status.m_BatchId != id ||
			!m_ParameterBatch->m_Status.IsTerminal())
		{
			return std::nullopt;
		}
		auto result = std::move(m_ParameterBatch->m_Status);
		m_ParameterBatch.reset();
		return result;
	}

	void SessionParameterControl::RefreshParameterBatchStatus() noexcept
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
				GGLAB_UNUSED(receipt.Fail("BatchEffectFailed: " + batch.m_Status.m_Failure));
			}
			batch.m_Status.m_Items.push_back(receipt.GetSnapshot());
			terminal &= receipt.GetSnapshot().IsTerminal();
		}
		if (terminal)
		{
			batch.m_Status.m_State = failed ? ParameterBatchState::Failed : ParameterBatchState::Finished;
		}
	}

	void SessionParameterControl::EndParameterBatch(bool superseded, std::string reason) noexcept
	{
		if (!m_ParameterBatch || m_ParameterBatch->m_Status.IsTerminal())
		{
			return;
		}
		auto& batch = *m_ParameterBatch;
		if (m_Lab)
		{
			m_Lab->CancelParameterBatch(batch.m_Status.m_BatchId);
		}
		if (batch.m_Receipts.empty())
		{
			batch.m_Status.m_State = ParameterBatchState::Failed;
			batch.m_Status.m_Failure = std::move(reason);
			return;
		}
		for (auto& receipt : batch.m_Receipts)
		{
			if (superseded)
			{
				GGLAB_UNUSED(receipt.Supersede());
			}
			else
			{
				GGLAB_UNUSED(receipt.Finish());
			}
		}
		batch.m_Consumed.clear();
		RefreshParameterBatchStatus();
	}

	void SessionParameterControl::OnRuntimeFailed(std::string_view reason) noexcept
	{
		m_ParameterIntakeFailed = true;
		if (m_Lab && m_ParameterBatch)
		{
			m_Lab->CancelParameterBatch(m_ParameterBatch->m_Status.m_BatchId);
		}
		if (m_ParameterBatch && !m_ParameterBatch->m_Status.IsTerminal())
		{
			for (auto& receipt : m_ParameterBatch->m_Receipts)
			{
				GGLAB_UNUSED(receipt.Fail(std::string(reason)));
			}
			RefreshParameterBatchStatus();
			m_ParameterBatch->m_Status.m_State = ParameterBatchState::Failed;
			m_ParameterBatch->m_Status.m_Failure = reason;
		}
	}

	void SessionParameterControl::Finish() noexcept
	{
		EndParameterBatch(false, "The runtime shut down before parameter consumption.");
		m_ParameterIntakeFailed = true;
		m_Demo = nullptr;
		m_Lab = nullptr;
	}

	void SessionParameterControl::OnFrameReady(uint64_t serial) noexcept
	{
		if (!m_ParameterBatch || m_ParameterBatch->m_Status.IsTerminal() || serial == 0)
		{
			return;
		}
		auto& batch = *m_ParameterBatch;
		batch.m_Consumed.clear();
		batch.m_FrameSerial = serial;
		if (batch.m_Session != m_ContentSerial)
		{
			EndParameterBatch(true, "The content changed before parameter application.");
			return;
		}
		batch.m_FrameBakeRequestGeneration = m_Environment.GetBakeRequestGeneration();
		if (batch.m_Status.m_State != ParameterBatchState::Queued)
		{
			return;
		}
		if (!IsAvailable() || m_ParameterRevision == std::numeric_limits<uint64_t>::max())
		{
			EndParameterBatch(false, "The content is unavailable at the safe parameter boundary.");
			return;
		}
		std::vector<LabParameterValue> hostValues;
		std::vector<LabParameterId> changed;
		for (const auto& value : batch.m_Status.m_Values)
		{
			const auto* current = Find(value.m_Id);
			if (!current)
			{
				EndParameterBatch(false, "The parameter source is unavailable.");
				return;
			}
			if (!LabValuesEqual(current->m_Value, value.m_Value))
			{
				changed.push_back(value.m_Id);
			}
			if (m_HostParameters.Find(value.m_Id))
			{
				hostValues.push_back(value);
			}
		}
		std::string error;
		auto host = m_HostParameters;
		if (!hostValues.empty() && !host.ApplyBatch(hostValues, error))
		{
			EndParameterBatch(false, std::move(error));
			return;
		}
		if (!batch.m_LabValues.empty() &&
			!m_Lab->ApplyParameterBatch(batch.m_Status.m_BatchId, batch.m_LabValues, error))
		{
			EndParameterBatch(false, std::move(error));
			return;
		}
		const uint64_t previousRevision = m_ParameterRevision;
		m_ObservedLabRevision = m_Lab ? m_Lab->GetParameterRevision() : 0;
		RefreshSources();
		m_ParameterRevision = previousRevision;
		host = m_HostParameters;
		for (const auto& value : hostValues)
		{
			if (!LabValuesEqual(host.Find(value.m_Id)->m_Value, value.m_Value) &&
				std::ranges::find(changed, value.m_Id) == changed.end())
			{
				changed.push_back(value.m_Id);
			}
		}
		if (!hostValues.empty())
		{
			GGLAB_UNUSED(host.ApplyBatch(hostValues, error));
		}
		m_HostParameters = std::move(host);
		for (const auto& value : hostValues)
		{
			if (std::ranges::find(changed, value.m_Id) == changed.end())
			{
				continue;
			}
			const auto found = std::ranges::find(m_HostOverrides, value.m_Id, &LabParameterValue::m_Id);
			if (found == m_HostOverrides.end())
			{
				m_HostOverrides.push_back(value);
			}
			else
			{
				*found = value;
			}
		}
		if (!changed.empty())
		{
			++m_ParameterRevision;
		}
		batch.m_Status.m_Revision = m_ParameterRevision;
		batch.m_Status.m_State = ParameterBatchState::Applying;
		for (const auto& value : batch.m_Status.m_Values)
		{
			auto receipt = ParameterEffectReceipt::Begin(value.m_Id,
				GetParameterEffectClass(Find(value.m_Id)->m_Desc), m_ParameterRevision, serial);
			GGLAB_ASSERT(receipt);
			if (std::ranges::find(changed, value.m_Id) == changed.end())
			{
				GGLAB_UNUSED(receipt->RecordNoChange(serial));
			}
			batch.m_Receipts.push_back(std::move(*receipt));
		}
		const uint64_t requested = m_Environment.GetBakeRequestGeneration();
		batch.m_ApplyBakeGeneration = requested != batch.m_FrameBakeRequestGeneration ? requested : 0;
		for (auto& receipt : batch.m_Receipts)
		{
			if (receipt.GetSnapshot().IsTerminal() ||
				receipt.GetSnapshot().m_DeclaredClass != ParameterEffectClass::Deferred)
			{
				continue;
			}
			const auto evidence = m_Lab->GetActiveSession()->GetLightingParameterRequest(receipt.GetSnapshot().m_Parameter);
			if (!evidence || evidence->m_PreviousGeneration != batch.m_FrameBakeRequestGeneration ||
				evidence->m_Generation != requested || requested == 0 ||
				requested - batch.m_FrameBakeRequestGeneration != 1)
			{
				GGLAB_UNUSED(receipt.Fail(
					"LightingEffectUnproven: the bound handler did not account for exactly one Runtime request."));
				continue;
			}
			batch.m_AttributedLightingRequest = receipt.RecordApplied(ParameterEffectClass::Deferred,
				{ batch.m_Status.m_Revision, "lighting", requested }, serial);
		}
		RefreshParameterBatchStatus();
	}

	void SessionParameterControl::ApplyViewSettings(ResolvedViewRenderSettings& settings,
		bool enablePreExposure) noexcept
	{
		if (!m_HostOverrides.empty())
		{
			ApplyHostViewParameters(m_HostOverrides, settings, enablePreExposure);
		}
	}

	void SessionParameterControl::OnFrameBuilt(uint64_t serial, uint64_t lightingGeneration,
		const ResolvedViewRenderSettings& settings) noexcept
	{
		if (!m_ParameterBatch || m_ParameterBatch->m_Status.IsTerminal() ||
			serial != m_ParameterBatch->m_FrameSerial || !m_Demo)
		{
			return;
		}
		auto& batch = *m_ParameterBatch;
		batch.m_Consumed.clear();
		const uint64_t bakeRequest = m_Environment.GetBakeRequestGeneration();
		const auto builtHost = DescribeHostViewParameters(settings);
		for (size_t index = 0; index < batch.m_Receipts.size(); ++index)
		{
			auto& receipt = batch.m_Receipts[index];
			if (receipt.GetSnapshot().IsTerminal())
			{
				continue;
			}
			const auto& requested = batch.m_Status.m_Values[index];
			const auto* current = Find(requested.m_Id);
			if (m_HostParameters.Find(requested.m_Id))
			{
				current = builtHost.Find(requested.m_Id);
			}
			if (!current || !LabValuesEqual(current->m_Value, requested.m_Value))
			{
				GGLAB_UNUSED(receipt.Supersede());
				continue;
			}
			if (receipt.GetSnapshot().m_DeclaredClass == ParameterEffectClass::Deferred)
			{
				const auto identity = receipt.GetSnapshot().m_Identity;
				if (lightingGeneration != 0 && lightingGeneration == identity.m_Generation)
				{
					// This generation came from the immutable built scene, not a later live query.
					GGLAB_UNUSED(receipt.RecordPublished(identity, serial));
					batch.m_Consumed.push_back({ index, identity });
				}
				else if (bakeRequest != identity.m_Generation)
				{
					GGLAB_UNUSED(receipt.Supersede());
				}
				else if (m_Environment.GetBakingStatus().m_Stage ==
					IBLBakeStage::Failed)
				{
					GGLAB_UNUSED(receipt.Fail("LightingPublicationFailed"));
				}
				continue;
			}
			if (batch.m_ApplyBakeGeneration != 0 && !batch.m_AttributedLightingRequest)
			{
				// Preserve actual application evidence even though it contradicts the
				// declaration. The owning Runtime supplied this request generation.
				GGLAB_UNUSED(receipt.RecordApplied(ParameterEffectClass::Deferred,
					{ batch.m_Status.m_Revision, "lighting", batch.m_ApplyBakeGeneration }, serial));
				continue;
			}
			if (bakeRequest != batch.m_FrameBakeRequestGeneration &&
				(!batch.m_AttributedLightingRequest || bakeRequest != batch.m_ApplyBakeGeneration))
			{
				// A later change has no proven attribution to the batch.
				GGLAB_UNUSED(receipt.Fail(
					"UnexpectedLightingTransition: effect attribution is unavailable for this frame."));
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

	void SessionParameterControl::OnFrameSubmitted(const DemoFrameFeedback& feedback) noexcept
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
			GGLAB_UNUSED(m_ParameterBatch->m_Receipts[consumed.m_Item].RecordSubmittedFrame(
				consumed.m_Identity, feedback.m_RenderFrameSerial));
		}
		m_ParameterBatch->m_Consumed.clear();
		RefreshParameterBatchStatus();
	}
}
