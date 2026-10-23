#include "Parameters/SessionParameterControl.h"
#include "Parameters/ParameterSourceAdapters.h"
#include "Demo/DemoBase.h"
#include "Lab/LabRuntime.h"
#include "GGLabRuntime/Graphics/RenderSceneTypes.h"

#include <algorithm>
#include <limits>
#include <utility>

namespace gglab
{
	SessionParameterControl::SessionParameterControl(RenderEnvironmentAccess& environment,
		WorldLightingControlBase* worldLighting,
		std::function<std::optional<ApplicationViewSettingsResolution>(DemoBase&)> viewResolver) noexcept :
		m_Sources(std::make_unique<ParameterSourceAdapters>(environment, worldLighting, std::move(viewResolver)))
	{
	}

	SessionParameterControl::~SessionParameterControl() = default;

	void SessionParameterControl::SynchronizeContent(DemoBase* demo, uint64_t contentIdentity,
		std::string_view demoId, bool transitioning) noexcept
	{
		const uint64_t serial = demo ? contentIdentity : 0;
		GGLAB_ASSERT_MSG(!demo || serial != 0, "Bound content requires its owner's frame identity.");
		if (demo != m_Demo || serial != m_ContentSerial)
		{
			// Release source reservations while their previous content owner is alive.
			EndParameterBatch(true, "The content changed before the parameter frame.");
			m_Sources->Bind(demo, demo ? demo->GetParameterLabRuntime() : nullptr, serial);
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

	const LabParameterSet& SessionParameterControl::GetHostParameters() const noexcept
	{
		return m_Sources->m_HostParameters;
	}

	const LabParameterSet* SessionParameterControl::GetLabParameters() const noexcept
	{
		return m_Sources->m_Lab.GetParameters();
	}

	std::span<const WorldLightingProfile> SessionParameterControl::GetLightingProfiles() const noexcept
	{
		return m_Sources->m_Profiles.GetProfiles();
	}

	const LightingProfilePublication& SessionParameterControl::GetLightingProfilePublication() const noexcept
	{
		return m_Sources->m_Profiles.GetPublication();
	}

	const std::string& SessionParameterControl::GetLightingProfileError() const noexcept
	{
		return m_Sources->m_Profiles.GetError();
	}

	const LabParameter* SessionParameterControl::Find(const LabParameterId& id) const noexcept
	{
		const auto* source = m_Sources->Find(id);
		return source ? source->GetParameters()->Find(id) : nullptr;
	}

	bool SessionParameterControl::SupportsParameterEffect(const LabParameterDesc& desc) const noexcept
	{
		const auto* source = m_Sources->Find(desc.m_Id);
		return source && source->Supports(desc);
	}

	void SessionParameterControl::RefreshSources() noexcept
	{
		if (!m_Demo || (m_Lab && !m_Lab->GetActiveSession()) || !m_Sources->m_View.HasDisplayView())
		{
			return;
		}
		const auto before = GetHostParameters().CaptureValues();
		m_Sources->Refresh(IsAvailable());
		const auto after = GetHostParameters().CaptureValues();
		const bool hostChanged = !before.empty() && !std::ranges::equal(before, after,
			[](const auto& a, const auto& b) { return a.m_Id == b.m_Id && LabValuesEqual(a.m_Value, b.m_Value); });
		const uint64_t labRevision = m_Lab ? m_Lab->GetParameterRevision() : 0;
		if ((hostChanged || (m_ObservedLabRevision != 0 && labRevision != m_ObservedLabRevision)) &&
			m_ParameterRevision != std::numeric_limits<uint64_t>::max())
		{
			++m_ParameterRevision;
		}
		m_ObservedLabRevision = labRevision;
	}

	bool SessionParameterControl::ValidateParameterBatch(
		std::span<const LabParameterValue> values, std::string& error) const noexcept
	{
		error.clear();
		if (!IsAvailable() || values.empty() || values.size() > SessionParameterMaxItems)
		{
			error = "A parameter batch requires ready active content and 1-128 items.";
			return false;
		}
		LabParameterSet candidate = GetHostParameters();
		if (const auto* lab = GetLabParameters())
		{
			for (const auto& parameter : lab->GetParameters())
			{
				if (!candidate.Add(parameter.m_Desc))
				{
					error = "Host and Lab parameter ids must be distinct.";
					return false;
				}
			}
		}
		for (const auto& value : values)
		{
			if (!m_Sources->ValidateMissing(value.m_Id, error))
			{
				return false;
			}
		}
		if (!candidate.ApplyBatch(values, error))
		{
			return false;
		}
		size_t deferredItems = 0;
		for (const auto& value : values)
		{
			auto* source = m_Sources->Find(value.m_Id);
			const auto& desc = candidate.Find(value.m_Id)->m_Desc;
			deferredItems += GetParameterEffectClass(desc) == ParameterEffectClass::Deferred;
			if (deferredItems > 1)
			{
				error = "At most one bound Deferred lighting item is supported per batch.";
				return false;
			}
			if (!source || !source->Supports(desc) || !source->Validate(value, error))
			{
				if (error.empty())
				{
					error = "Unsupported effect: the owning parameter source has no binding.";
				}
				return false;
			}
		}
		return true;
	}

	bool SessionParameterControl::ValidateImmediateAssignments(
		std::span<LabParameterValue> values, std::string& error) const noexcept
	{
		for (auto& value : values)
		{
			const auto* parameter = Find(value.m_Id);
			if (!parameter || GetParameterEffectClass(parameter->m_Desc) != ParameterEffectClass::Immediate)
			{
				error = "Sequence events currently require bound Immediate parameters.";
				return false;
			}
			// Untyped integer assignments resolve against their owning descriptor. Resolve only
			// integer representations; booleans, arrays and fractional integers stay strict.
			if (const auto* number = std::get_if<uint32_t>(&value.m_Value))
			{
				if (parameter->m_Desc.m_Type == LabParameterType::Float)
				{
					value.m_Value = static_cast<float>(*number);
				}
				else if ((parameter->m_Desc.m_Type == LabParameterType::Int ||
					parameter->m_Desc.m_Type == LabParameterType::Enum) && *number <= INT32_MAX)
				{
					value.m_Value = static_cast<int32_t>(*number);
				}
			}
			else if (const auto* signedNumber = std::get_if<int32_t>(&value.m_Value))
			{
				if (parameter->m_Desc.m_Type == LabParameterType::Float)
				{
					value.m_Value = static_cast<float>(*signedNumber);
				}
				else if (parameter->m_Desc.m_Type == LabParameterType::UInt && *signedNumber >= 0)
				{
					value.m_Value = static_cast<uint32_t>(*signedNumber);
				}
			}
		}
		return ValidateParameterBatch(values, error);
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
		if (!ValidateParameterBatch(values, error))
		{
			return 0;
		}
		std::vector<ParameterSourceBase*> bindings;
		bool hasHost = false;
		bool hasLab = false;
		for (const auto& value : values)
		{
			auto* source = m_Sources->Find(value.m_Id);
			bindings.push_back(source);
			hasHost |= source->IsHost();
			hasLab |= !source->IsHost();
		}
		const uint64_t id = m_NextParameterBatchId;
		std::vector<ParameterSourceBase*> reserved;
		reserved.reserve(m_Sources->Ordered().size());
		for (auto* source : m_Sources->Ordered())
		{
			std::vector<LabParameterValue> group;
			for (size_t index = 0; index < values.size(); ++index)
			{
				if (bindings[index] == source)
				{
					group.push_back(values[index]);
				}
			}
			if (group.empty())
			{
				continue;
			}
			if (!source->Reserve(id, group, error))
			{
				// Failure does not transfer a source's existing reservation to us.
				for (auto* acquired : reserved)
				{
					acquired->Cancel(id);
				}
				return 0;
			}
			reserved.push_back(source);
		}
		++m_NextParameterBatchId;
		m_ParameterBatch.emplace();
		auto& batch = *m_ParameterBatch;
		batch.m_Status.m_BatchId = id;
		batch.m_Status.m_ContentId = m_ContentId;
		batch.m_Status.m_Source = hasHost ? (hasLab ? "mixed" : "host") : "lab";
		batch.m_Session = m_ContentSerial;
		batch.m_Status.m_Values.assign(values.begin(), values.end());
		batch.m_Bindings = std::move(bindings);
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

	void SessionParameterControl::FinishParameterBatch(uint64_t id) noexcept
	{
		if (m_ParameterBatch && m_ParameterBatch->m_Status.m_BatchId == id)
		{
			EndParameterBatch(false, "The sequence ended before parameter consumption.");
		}
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
		for (auto* source : m_Sources->Ordered())
		{
			source->Cancel(batch.m_Status.m_BatchId);
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
		if (m_ParameterBatch)
		{
			for (auto* source : m_Sources->Ordered())
			{
				source->Cancel(m_ParameterBatch->m_Status.m_BatchId);
			}
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
		m_Sources->Bind(nullptr, nullptr, 0);
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
		m_Sources->m_Profiles.Synchronize();
		RefreshSources();
		m_Sources->m_Domain.BeginFrame(batch.m_Domain);
		if (batch.m_Status.m_State != ParameterBatchState::Queued)
		{
			return;
		}
		if (!IsAvailable() || m_ParameterRevision == std::numeric_limits<uint64_t>::max())
		{
			EndParameterBatch(false, "The content is unavailable at the safe parameter boundary.");
			return;
		}
		if (!m_Sources->m_View.HasDisplayView())
		{
			EndParameterBatch(false, "ParameterPreparationFailed: the display view is unavailable.");
			return;
		}
		struct SourcePlan
		{
			ParameterSourceBase* m_Source = nullptr;
			std::unique_ptr<PreparedParameterSourceBase> m_Prepared;
		};
		std::vector<SourcePlan> plans;
		plans.reserve(m_Sources->Ordered().size());
		size_t deferredItems = 0;
		for (size_t index = 0; index < batch.m_Status.m_Values.size(); ++index)
		{
			const auto& value = batch.m_Status.m_Values[index];
			const auto* current = Find(value.m_Id);
			if (!current || batch.m_Bindings[index] != m_Sources->Find(value.m_Id) ||
				!SupportsParameterEffect(current->m_Desc))
			{
				EndParameterBatch(false, "ParameterPreparationFailed: a source or supported effect changed.");
				return;
			}
			deferredItems += GetParameterEffectClass(current->m_Desc) == ParameterEffectClass::Deferred;
			if (deferredItems > 1)
			{
				EndParameterBatch(false, "ParameterPreparationFailed: at most one bound lighting item is supported.");
				return;
			}
		}
		std::string error;
		bool changed = false;
		// Prepare every owning source before consuming reservations or mutating state.
		for (auto* source : m_Sources->Ordered())
		{
			std::vector<LabParameterValue> values;
			for (size_t index = 0; index < batch.m_Status.m_Values.size(); ++index)
			{
				if (batch.m_Bindings[index] == source)
				{
					values.push_back(batch.m_Status.m_Values[index]);
				}
			}
			if (values.empty())
			{
				continue;
			}
			auto prepared = source->Prepare(batch.m_Status.m_BatchId, values, error);
			if (!prepared)
			{
				EndParameterBatch(false, std::move(error));
				return;
			}
			changed |= prepared->HasChanges();
			plans.push_back({ source, std::move(prepared) });
		}
		const uint64_t revision = m_ParameterRevision + (changed ? 1 : 0);
		std::vector<ParameterEffectReceipt> receipts;
		receipts.reserve(batch.m_Status.m_Values.size());
		std::vector<bool> noChange(batch.m_Status.m_Values.size(), false);
		for (const auto& value : batch.m_Status.m_Values)
		{
			auto receipt = ParameterEffectReceipt::Begin(value.m_Id,
				GetParameterEffectClass(Find(value.m_Id)->m_Desc), revision, serial);
			if (!receipt)
			{
				EndParameterBatch(false, "ParameterPreparationFailed: receipt identity is unavailable.");
				return;
			}
			receipts.push_back(std::move(*receipt));
		}

		// No callbacks or rejection in this region. Install every source and the
		// unique receipt owner before allowing any content handler to observe state.
		for (auto& plan : plans)
		{
			plan.m_Prepared->Commit();
		}
		m_Sources->PublishHostParameters();
		m_ParameterRevision = revision;
		m_ObservedLabRevision = m_Lab ? m_Lab->GetParameterRevision() : 0;
		batch.m_Receipts = std::move(receipts);
		batch.m_NoChange = std::move(noChange);
		batch.m_Status.m_Revision = revision;
		batch.m_Status.m_State = ParameterBatchState::Applying;
		RefreshParameterBatchStatus();

		for (auto& plan : plans)
		{
			plan.m_Prepared->ApplyEffects();
		}
		for (auto& plan : plans)
		{
			plan.m_Prepared->FinalizeEffects();
			GGLAB_ASSERT_MSG(changed || !plan.m_Prepared->HasChanges(),
				"Inherited values cannot change during an all-NoChange batch without an owning source effect.");
		}
		RefreshSources();
		m_ParameterRevision = revision;
		m_Sources->m_Domain.CompleteEffects(batch.m_Domain);
		for (size_t index = 0; index < batch.m_Receipts.size(); ++index)
		{
			auto& receipt = batch.m_Receipts[index];
			const auto found = std::ranges::find(plans, batch.m_Bindings[index], &SourcePlan::m_Source);
			GGLAB_ASSERT(found != plans.end());
			auto application = found->m_Prepared->DescribeApplication(receipt.GetSnapshot().m_Parameter, batch.m_Domain);
			if (!application.m_Failure.empty())
			{
				GGLAB_UNUSED(receipt.Fail(std::move(application.m_Failure)));
			}
			else if (receipt.GetSnapshot().m_DeclaredClass == ParameterEffectClass::Immediate)
			{
				batch.m_NoChange[index] = application.m_NoChange;
			}
			else if (application.m_NoChange)
			{
				GGLAB_UNUSED(receipt.RecordNoChange(serial));
			}
			else
			{
				application.m_Identity.m_Revision = revision;
				GGLAB_UNUSED(receipt.RecordApplied(application.m_ActualClass, application.m_Identity, serial));
			}
		}
		RefreshParameterBatchStatus();
	}

	void SessionParameterControl::ApplyViewSettings(ResolvedViewRenderSettings& settings,
		bool enablePreExposure) noexcept
	{
		m_Sources->m_View.ApplyViewSettings(settings, enablePreExposure);
	}

	void SessionParameterControl::OnFrameBuilt(uint64_t serial, uint64_t lightingGeneration,
		const ResolvedViewRenderSettings& settings, const RenderView* view) noexcept
	{
		if (m_Enabled && m_Demo)
		{
			m_Sources->m_Profiles.ObservePublication(lightingGeneration);
		}
		if (!m_ParameterBatch || m_ParameterBatch->m_Status.IsTerminal() ||
			serial != m_ParameterBatch->m_FrameSerial || !m_Demo)
		{
			return;
		}
		auto& batch = *m_ParameterBatch;
		batch.m_Consumed.clear();
		const ParameterSourceFrame frame{ settings, view, lightingGeneration };
		for (size_t index = 0; index < batch.m_Receipts.size(); ++index)
		{
			auto& receipt = batch.m_Receipts[index];
			if (receipt.GetSnapshot().IsTerminal())
			{
				continue;
			}
			const auto& requested = batch.m_Status.m_Values[index];
			auto* source = batch.m_Bindings[index];
			if (source != m_Sources->Find(requested.m_Id) || !source->MatchesFrame(requested, frame))
			{
				GGLAB_UNUSED(receipt.Supersede());
				continue;
			}
			if (batch.m_NoChange[index])
			{
				GGLAB_UNUSED(receipt.RecordNoChange(serial));
				continue;
			}
			if (receipt.GetSnapshot().m_DeclaredClass == ParameterEffectClass::Deferred)
			{
				const auto identity = receipt.GetSnapshot().m_Identity;
				auto observation = source->ObservePublication(identity, frame);
				switch (observation.m_State)
				{
				case ParameterPublication::Published:
					GGLAB_UNUSED(receipt.RecordPublished(identity, serial));
					batch.m_Consumed.push_back({ index, identity });
					break;
				case ParameterPublication::Superseded:
					GGLAB_UNUSED(receipt.Supersede());
					break;
				case ParameterPublication::Failed:
					GGLAB_UNUSED(receipt.Fail(std::move(observation.m_Failure)));
					break;
				case ParameterPublication::Pending:
					break;
				}
				continue;
			}
			auto application = m_Sources->m_Domain.ObserveImmediate(batch.m_Domain);
			if (!application.m_Failure.empty())
			{
				GGLAB_UNUSED(receipt.Fail(std::move(application.m_Failure)));
				continue;
			}
			application.m_Identity.m_Revision = batch.m_Status.m_Revision;
			if (receipt.RecordApplied(application.m_ActualClass, application.m_Identity, serial))
			{
				batch.m_Consumed.push_back({ index, application.m_Identity });
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
