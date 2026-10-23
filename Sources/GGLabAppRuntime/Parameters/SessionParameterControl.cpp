#include "Parameters/SessionParameterControl.h"
#include "Parameters/HostViewParameters.h"
#include "Demo/DemoBase.h"
#include "Lab/LabRuntime.h"
#include "GGLabFoundation/Base/CoreMacros.h"
#include "GGLabRuntime/Graphics/CameraRig.h"
#include "GGLabRuntime/Graphics/RenderSceneTypes.h"
#include "GGLabRuntime/Graphics/RenderServices.h"
#include "GGLabRuntime/Graphics/WorldLightingControlBase.h"

#include <algorithm>
#include <limits>
#include <memory>
#include <utility>
#include <string>
#include <vector>

namespace gglab
{
	SessionParameterControl::SessionParameterControl(RenderEnvironmentAccess& environment,
		WorldLightingControlBase* worldLighting,
		std::function<std::optional<ApplicationViewSettingsResolution>(DemoBase&)> viewResolver) noexcept :
		m_ViewResolver(std::move(viewResolver)), m_Environment(environment), m_WorldLighting(worldLighting)
	{
	}

	void SessionParameterControl::SynchronizeContent(DemoBase* demo, uint64_t contentIdentity,
		std::string_view demoId, bool transitioning) noexcept
	{
		const uint64_t serial = demo ? contentIdentity : 0;
		GGLAB_ASSERT_MSG(!demo || serial != 0, "Bound content requires its owner's frame identity.");
		if (demo != m_Demo || serial != m_ContentSerial)
		{
			// The lifecycle owner releases Demo borrows before exit/retirement. A Lab
			// session replacement retains the same live, command-owning LabRuntime.
			EndParameterBatch(true, "The content changed before the parameter frame.");
			m_Lab = nullptr;
			m_HostOverrides.clear();
			m_HostParameters = {};
			m_LightingProfiles.clear();
			m_PreparedLightingProfiles.clear();
			m_LightingProfileHashes.clear();
			m_ProfilesInitialized = false;
			m_ProfileDescriptor.reset();
			m_LightingProfilesState = {};
			m_ProfileRegistrationError.clear();
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

	std::optional<ApplicationViewSettingsResolution>
	SessionParameterControl::ResolveInheritedViewSettings() const noexcept
	{
		if (!m_Demo)
		{
			return std::nullopt;
		}
		if (m_ViewResolver)
		{
			return m_ViewResolver(*m_Demo);
		}
		const auto view = m_Demo->GetCameraRig().ResolveEffectiveDisplayView();
		return view.IsValid() ?
			std::optional(ResolveApplicationViewSettings(m_Demo->GetViewRenderProfile(),
				*view.m_CameraSlot->m_Camera, {})) :
			std::nullopt;
	}

	void SessionParameterControl::RefreshSources() noexcept
	{
		if (!m_Demo || (m_Lab && !m_Lab->GetActiveSession()))
		{
			return;
		}
		const auto inherited = ResolveInheritedViewSettings();
		if (!inherited)
		{
			return;
		}
		auto host = DescribeHostViewParameters(inherited->m_View);
		RefreshLightingProfiles(host);
		std::string error;
		if (!m_HostOverrides.empty())
		{
			const auto applied = host.ApplyBatch(m_HostOverrides, error);
			GGLAB_ASSERT_MSG(applied.has_value(), "Committed Host overrides must match the fixed Host schema.");
			GGLAB_UNUSED(applied);
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
		for (const auto& value : values)
		{
			if (value.m_Id.GetName() == "environment.profile" && !candidate.Find(value.m_Id) &&
				!m_ProfileRegistrationError.empty())
			{
				error = m_ProfileRegistrationError;
				return 0;
			}
		}
		if (!candidate.ApplyBatch(values, error))
		{
			return 0;
		}
		std::vector<LabParameterValue> labValues;
		size_t lightingItems = 0;
		bool hasHost = false;
		for (const auto& value : values)
		{
			lightingItems += GetParameterEffectClass(candidate.Find(value.m_Id)->m_Desc) == ParameterEffectClass::Deferred;
			if (lightingItems > 1)
			{
				error = "At most one bound Deferred lighting item is supported per batch.";
				return 0;
			}
			if (IsLightingProfileParameter(value.m_Id) &&
				(!std::get_if<int32_t>(&value.m_Value) || std::get<int32_t>(value.m_Value) < 0 ||
					!CanApplyWorldLightingInputs(m_Demo->GetWorld())))
			{
				error = "Select a registered lighting profile; custom is read-only and requires a designated World sun.";
				return 0;
			}
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
		// Settle pre-existing authored edits before this batch's attribution boundary.
		if (m_WorldLighting && m_Demo)
		{
			m_WorldLighting->SynchronizeWorldLighting(m_Demo->GetWorld(), m_ContentSerial);
		}
		RefreshSources();
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
		// Prepare the complete batch without changing values, consuming a Lab command,
		// invoking content callbacks or creating this batch's lighting request.
		struct PreparedBatch
		{
			LabParameterSet m_Host;
			std::vector<LabParameterValue> m_HostValues;
			std::vector<LabParameterValue> m_PreviousOverrides;
			std::vector<LabParameterValue> m_Overrides;
			std::vector<LabParameterId> m_NewOverrides;
			std::vector<LabParameterId> m_Changed;
			std::vector<ParameterEffectReceipt> m_Receipts;
			std::vector<bool> m_NoChange;
			std::optional<LabRuntime::PreparedParameterBatch> m_Lab;
			std::unique_ptr<PreparedWorldLightingMutationBase> m_Lighting;
			uint64_t m_Revision = 0;
		};
		PreparedBatch prepared;
		if (!m_Demo->GetCameraRig().ResolveEffectiveDisplayView().IsValid())
		{
			EndParameterBatch(false, "ParameterPreparationFailed: the display view is unavailable.");
			return;
		}
		prepared.m_Host = m_HostParameters;
		prepared.m_PreviousOverrides = m_HostOverrides;
		prepared.m_Overrides = m_HostOverrides;
		prepared.m_Overrides.reserve(m_HostOverrides.size() + batch.m_Status.m_Values.size());
		prepared.m_Changed.reserve(batch.m_Status.m_Values.size());
		prepared.m_Receipts.reserve(batch.m_Status.m_Values.size());
		prepared.m_NoChange.resize(batch.m_Status.m_Values.size(), false);
		size_t lightingItems = 0;
		for (const auto& value : batch.m_Status.m_Values)
		{
			const auto* current = Find(value.m_Id);
			const bool fromLab = std::ranges::any_of(batch.m_LabValues,
				[&](const auto& item) { return item.m_Id == value.m_Id; });
			const bool fromHost = m_HostParameters.Find(value.m_Id) != nullptr;
			const auto* labParameters = GetLabParameters();
			// The retained admission binding must still name exactly this source.
			if (!current || fromLab == fromHost ||
				(fromHost && labParameters && labParameters->Find(value.m_Id)) ||
				!SupportsParameterEffect(current->m_Desc))
			{
				EndParameterBatch(false, "ParameterPreparationFailed: a source or supported effect changed.");
				return;
			}
			lightingItems += GetParameterEffectClass(current->m_Desc) == ParameterEffectClass::Deferred;
			if (lightingItems > 1)
			{
				EndParameterBatch(false, "ParameterPreparationFailed: at most one bound lighting item is supported.");
				return;
			}
			if (!LabValuesEqual(current->m_Value, value.m_Value))
			{
				prepared.m_Changed.push_back(value.m_Id);
			}
			if (m_HostParameters.Find(value.m_Id))
			{
				prepared.m_HostValues.push_back(value);
			}
		}
		std::string error;
		if (!prepared.m_HostValues.empty() && !prepared.m_Host.ApplyBatch(prepared.m_HostValues, error))
		{
			EndParameterBatch(false, std::move(error));
			return;
		}
		if (!batch.m_LabValues.empty())
		{
			prepared.m_Lab = m_Lab->PrepareParameterBatch(batch.m_Status.m_BatchId, batch.m_LabValues, error);
			if (!prepared.m_Lab)
			{
				EndParameterBatch(false, std::move(error));
				return;
			}
		}
		for (const auto& value : prepared.m_HostValues)
		{
			if (IsLightingProfileParameter(value.m_Id))
			{
				const auto* index = std::get_if<int32_t>(&value.m_Value);
				if (!index || *index < 0 || static_cast<size_t>(*index) >= m_LightingProfiles.size())
				{
					EndParameterBatch(false, "ParameterPreparationFailed: select a registered lighting profile.");
					return;
				}
				prepared.m_Lighting = m_WorldLighting->PrepareWorldLightingInputs(m_Demo->GetWorld(),
					m_ContentSerial, m_LightingProfiles[*index].m_Inputs, error);
				if (!prepared.m_Lighting)
				{
					EndParameterBatch(false, std::move(error));
					return;
				}
				if (!prepared.m_Lighting->HasChanges())
				{
					std::erase(prepared.m_Changed, value.m_Id);
				}
				continue;
			}
			const auto found = std::ranges::find(prepared.m_Overrides, value.m_Id, &LabParameterValue::m_Id);
			if (found == prepared.m_Overrides.end())
			{
				prepared.m_Overrides.push_back(value);
				prepared.m_NewOverrides.push_back(value.m_Id);
			}
			else
			{
				*found = value;
			}
		}
		prepared.m_Revision = m_ParameterRevision + (prepared.m_Changed.empty() ? 0 : 1);
		for (const auto& value : batch.m_Status.m_Values)
		{
			auto receipt = ParameterEffectReceipt::Begin(value.m_Id,
				GetParameterEffectClass(Find(value.m_Id)->m_Desc), prepared.m_Revision, serial);
			if (!receipt)
			{
				EndParameterBatch(false, "ParameterPreparationFailed: receipt identity is unavailable.");
				return;
			}
			prepared.m_Receipts.push_back(std::move(*receipt));
		}

		// Commit is uninterrupted and cannot reject. Install every source before
		// callbacks: a Lab handler must never observe half-committed batch values.
		std::optional<LightingMutationResult> profileEvidence;
		if (prepared.m_Lighting)
		{
			profileEvidence = prepared.m_Lighting->Commit();
		}
		if (prepared.m_Lab)
		{
			m_Lab->CommitParameterBatch(*prepared.m_Lab);
		}
		m_HostParameters = std::move(prepared.m_Host);
		m_HostOverrides = std::move(prepared.m_Overrides);
		m_ParameterRevision = prepared.m_Revision;
		m_ObservedLabRevision = m_Lab ? m_Lab->GetParameterRevision() : 0;
		batch.m_Receipts = std::move(prepared.m_Receipts);
		batch.m_NoChange = std::move(prepared.m_NoChange);
		batch.m_Status.m_Revision = prepared.m_Revision;
		batch.m_Status.m_State = ParameterBatchState::Applying;
		RefreshParameterBatchStatus();

		// Effects start only after commit. Their failures/supersession retain the
		// existing asynchronous receipt semantics, not transactional GPU rollback.
		std::optional<LabRuntime::LightingParameterEffect> labEvidence;
		if (prepared.m_Lab)
		{
			labEvidence = m_Lab->ApplyParameterBatchEffects(std::move(*prepared.m_Lab));
		}
		const auto resolvedInherited = ResolveInheritedViewSettings();
		if (resolvedInherited)
		{
			auto inheritedSettings = resolvedInherited->m_View;
			ApplyHostViewParameters(prepared.m_PreviousOverrides, inheritedSettings,
				resolvedInherited->m_Profile.m_EnableScenePreExposure);
			const auto inherited = DescribeHostViewParameters(inheritedSettings);
			for (const auto& value : prepared.m_HostValues)
			{
				if (!IsLightingProfileParameter(value.m_Id) &&
					!LabValuesEqual(inherited.Find(value.m_Id)->m_Value, value.m_Value) &&
					std::ranges::find(prepared.m_Changed, value.m_Id) == prepared.m_Changed.end())
				{
					// A Lab effect can change an inherited field that initially matched
					// the Host request. The explicit Host value still takes precedence.
					GGLAB_ASSERT(prepared.m_Lab && prepared.m_Lab->HasChanges());
					prepared.m_Changed.push_back(value.m_Id);
				}
			}
		}
		for (const auto& id : prepared.m_NewOverrides)
		{
			if (std::ranges::find(prepared.m_Changed, id) == prepared.m_Changed.end())
			{
				std::erase_if(m_HostOverrides, [&](const auto& value) { return value.m_Id == id; });
			}
		}
		for (size_t index = 0; index < batch.m_Receipts.size(); ++index)
		{
			auto& receipt = batch.m_Receipts[index];
			if (std::ranges::find(prepared.m_Changed, receipt.GetSnapshot().m_Parameter) == prepared.m_Changed.end())
			{
				if (receipt.GetSnapshot().m_DeclaredClass == ParameterEffectClass::Immediate)
				{
					// Equal authoring values still require an effective-frame projection:
					// tooling or Host precedence can mask an unchanged Lab request.
					batch.m_NoChange[index] = true;
				}
			}
		}
		RefreshSources();
		m_ParameterRevision = prepared.m_Revision;
		const uint64_t requested = m_Environment.GetBakeRequestGeneration();
		batch.m_ApplyBakeGeneration = requested != batch.m_FrameBakeRequestGeneration ? requested : 0;
		for (auto& receipt : batch.m_Receipts)
		{
			if (receipt.GetSnapshot().IsTerminal() ||
				receipt.GetSnapshot().m_DeclaredClass != ParameterEffectClass::Deferred)
			{
				continue;
			}
			const auto& parameter = receipt.GetSnapshot().m_Parameter;
			std::optional<LightingMutationResult> evidence = profileEvidence;
			if (!IsLightingProfileParameter(parameter))
			{
				evidence = labEvidence && labEvidence->m_Parameter == parameter ?
					std::optional(labEvidence->m_Result) :
					std::nullopt;
			}
			// The operation must cover the entire attribution boundary. Its own domain
			// may issue multiple requests, or bind an existing target without a request.
			const bool requestedTarget = evidence && evidence->m_Outcome == LightingMutationOutcome::Requested &&
				evidence->m_Generation > evidence->m_PreviousGeneration;
			const bool existingTarget = evidence && evidence->m_Outcome == LightingMutationOutcome::Unchanged &&
				evidence->m_Generation == evidence->m_PreviousGeneration;
			if ((!requestedTarget && !existingTarget) ||
				evidence->m_PreviousGeneration != batch.m_FrameBakeRequestGeneration ||
				evidence->m_Generation != requested || requested == 0)
			{
				GGLAB_UNUSED(receipt.Fail(
					"LightingEffectUnproven: the current operation did not bind the Runtime target at this boundary."));
				continue;
			}
			if (existingTarget && m_Environment.GetBakingStatus().m_ActiveGeneration == requested &&
				std::ranges::find(prepared.m_Changed, parameter) == prepared.m_Changed.end())
			{
				// Storage equality alone cannot terminate a pending bake as NoChange.
				batch.m_AttributedLightingRequest = receipt.RecordNoChange(serial);
			}
			else
			{
				batch.m_AttributedLightingRequest = receipt.RecordApplied(ParameterEffectClass::Deferred,
					{ batch.m_Status.m_Revision, "lighting", requested }, serial);
			}
			if (batch.m_AttributedLightingRequest)
			{
				batch.m_ApplyBakeGeneration = requested;
			}
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
		const ResolvedViewRenderSettings& settings, const RenderView* view) noexcept
	{
		if (m_Enabled && m_Demo)
		{
			UpdateLightingProfilePublication(lightingGeneration);
		}
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
			const bool isProfile = IsLightingProfileParameter(requested.m_Id);
			std::optional<LabValue> frameValue;
			if (m_HostParameters.Find(requested.m_Id) && !isProfile)
			{
				current = builtHost.Find(requested.m_Id);
			}
			else if (!isProfile && receipt.GetSnapshot().m_DeclaredClass == ParameterEffectClass::Immediate)
			{
				const auto* session = m_Lab ? m_Lab->GetActiveSession() : nullptr;
				frameValue = session ?
					session->ReadImmediateParameterFrameValue(requested.m_Id, { settings, view }) :
					std::nullopt;
				current = nullptr;
			}
			const LabValue* effective = frameValue ? &*frameValue : (current ? &current->m_Value : nullptr);
			if ((isProfile && FindCurrentLightingProfile() != std::get<int32_t>(requested.m_Value)) ||
				!effective || !LabValuesEqual(*effective, requested.m_Value))
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
