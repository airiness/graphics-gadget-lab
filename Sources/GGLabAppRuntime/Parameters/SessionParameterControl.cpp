#include "Parameters/SessionParameterControl.h"
#include "Parameters/ParameterSourceAdapters.h"
#include "Capture/FrameCaptureReadiness.h"
#include "Demo/DemoBase.h"
#include "Lab/LabRuntime.h"
#include "GGLabRuntime/Core/World.h"
#include "GGLabRuntime/Graphics/WorldLightingControlBase.h"
#include "GGLabRuntime/Graphics/RenderSceneTypes.h"

#include <algorithm>
#include <format>
#include <limits>
#include <string_view>
#include <utility>

namespace gglab
{
	namespace
	{
		[[nodiscard]] bool SameOverrides(std::span<const LabParameterValue> a,
			std::span<const LabParameterValue> b) noexcept
		{
			return a.size() == b.size() && std::ranges::all_of(a, [&](const LabParameterValue& value)
				{
					const auto found = std::ranges::find(b, value.m_Id, &LabParameterValue::m_Id);
					return found != b.end() && LabValuesEqual(found->m_Value, value.m_Value);
				});
		}

		[[nodiscard]] std::optional<LabValue> FindOverride(std::span<const LabParameterValue> overrides,
			const LabParameterId& id) noexcept
		{
			const auto found = std::ranges::find(overrides, id, &LabParameterValue::m_Id);
			return found != overrides.end() ? std::optional(found->m_Value) : std::nullopt;
		}

	}

	SessionParameterControl::SessionParameterControl(RenderEnvironmentAccess& environment,
		WorldLightingControlBase* worldLighting,
		std::function<std::optional<ApplicationViewSettingsResolution>(DemoBase&)> viewResolver) noexcept :
		m_Sources(std::make_unique<ParameterSourceAdapters>(environment, worldLighting, std::move(viewResolver))),
		m_WorldLighting(worldLighting)
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
			if (m_ActivationRevision != std::numeric_limits<uint64_t>::max())
			{
				++m_ActivationRevision;
			}
			bool continuing = false;
			// Release source reservations while their previous content owner is alive.
			for (auto& batch : m_ParameterBatches)
			{
				if (!batch->m_Status.IsTerminal() && batch->m_WaitingForContent && demo == m_Demo &&
					demo && demo->GetParameterLabRuntime() == m_Lab &&
					std::ranges::any_of(batch->m_Bindings, [&](const auto* source)
					{
						return source->ContinuesContentChange(batch->m_Status.m_BatchId, m_ContentSerial, serial);
					}))
				{
					batch->m_Session = serial;
					batch->m_WaitingForContent = false;
					continuing = true;
				}
				else
				{
					EndForContentChange(*batch, "The content changed before the parameter frame.");
				}
			}
			const bool discardsHostView = !continuing && !GetHostViewOverrides().empty();
			m_Sources->Bind(demo, demo ? demo->GetParameterLabRuntime() : nullptr, serial, continuing);
			if (discardsHostView)
			{
				AdvanceHostViewRevision();
			}
			if (!continuing && m_Enabled && m_Demo &&
				m_ParameterRevision != std::numeric_limits<uint64_t>::max())
			{
				++m_ParameterRevision;
			}
			m_ContentSerial = serial;
			m_ObservedLabRevision = continuing && m_Lab ? m_Lab->GetParameterRevision() : 0;
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
		for (auto& batch : m_ParameterBatches)
		{
			if (batch->m_Status.IsTerminal())
			{
				continue;
			}
			for (size_t index = 0; index < batch->m_Receipts.size(); ++index)
			{
				auto& receipt = batch->m_Receipts[index];
				if (receipt.GetSnapshot().IsTerminal())
				{
					continue;
				}
				const auto observation = batch->m_Bindings[index]->ObserveLifetime(receipt.GetSnapshot().m_Identity);
				if (observation.m_State == ParameterPublication::Failed)
				{
					GGLAB_UNUSED(receipt.Fail(observation.m_Failure));
				}
				else if (observation.m_State == ParameterPublication::Superseded)
				{
					EndParameterBatch(*batch, true, "The owning effect was superseded.");
					break;
				}
			}
			RefreshParameterBatchStatus(*batch);
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

	std::span<const LabParameterValue> SessionParameterControl::GetHostViewOverrides() const noexcept
	{
		return m_Sources->m_View.GetOverrides();
	}

	std::vector<HostViewProjectionEntry> SessionParameterControl::DescribeHostView() const
	{
		const auto resolution = m_Sources->m_View.Resolve();
		return resolution ?
			DescribeHostViewProjection(resolution->m_View, resolution->m_ToolingBlocks, GetHostViewOverrides()) :
			std::vector<HostViewProjectionEntry>{};
	}

	void SessionParameterControl::AdvanceHostViewRevision() noexcept
	{
		// Admission and application stop at the last value, so no revision is reused.
		GGLAB_ASSERT_MSG(m_HostViewRevision != std::numeric_limits<uint64_t>::max(),
			"Host view revision capacity is exhausted.");
		if (m_HostViewRevision != std::numeric_limits<uint64_t>::max())
		{
			++m_HostViewRevision;
		}
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
		bool hasRebuild = false;
		for (const auto& value : values)
		{
			auto* source = m_Sources->Find(value.m_Id);
			const auto& desc = candidate.Find(value.m_Id)->m_Desc;
			deferredItems += GetParameterEffectClass(desc) == ParameterEffectClass::Deferred;
			hasRebuild |= GetParameterEffectClass(desc) == ParameterEffectClass::Rebuild;
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
		if (hasRebuild && deferredItems != 0)
		{
			error = "Rebuild transactions cannot include Deferred lighting bound to the old content.";
			return false;
		}
		return true;
	}

	bool SessionParameterControl::ValidateSequenceAssignments(std::span<LabParameterValue> values,
		const std::optional<std::vector<std::string>>& expectedEffects, std::string& error) const noexcept
	{
		std::vector<std::string> domains;
		for (auto& value : values)
		{
			const auto* parameter = Find(value.m_Id);
			if (!parameter || GetParameterEffectClass(parameter->m_Desc) == ParameterEffectClass::Rebuild)
			{
				error = "Sequence events require bound Immediate or Deferred parameters without rebuild impact.";
				return false;
			}
			if (GetParameterEffectClass(parameter->m_Desc) == ParameterEffectClass::Deferred)
			{
				const auto* source = m_Sources->Find(value.m_Id);
				const auto domain = source ? source->GetDeferredEffectDomain() : std::string_view{};
				if (domain.empty())
				{
					error = "The Deferred event source has no bound effect domain.";
					return false;
				}
				if (std::ranges::find(domains, domain) == domains.end())
				{
					domains.emplace_back(domain);
				}
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
		if (!ValidateParameterBatch(values, error))
		{
			return false;
		}
		if (expectedEffects && (expectedEffects->size() != domains.size() ||
			!std::ranges::all_of(domains, [&](const auto& domain)
			{
				return std::ranges::find(*expectedEffects, domain) != expectedEffects->end();
			})))
		{
			error = "EffectMismatch: expectEffects must match the event's bound Deferred domains.";
			return false;
		}
		return true;
	}

	bool SessionParameterControl::CoversReadinessGate(ParameterBatchOwner owner,
		const FrameCaptureGate& gate) const noexcept
	{
		if (owner.IsExternal() || gate.m_EffectDomain.empty() || gate.m_EffectGeneration == 0)
		{
			return false;
		}
		for (const auto& batch : m_ParameterBatches)
		{
			if (batch->m_Owner != owner || batch->m_Status.IsTerminal())
			{
				continue;
			}
			for (size_t index = 0; index < batch->m_Receipts.size(); ++index)
			{
				const auto& item = batch->m_Receipts[index].GetSnapshot();
				if (!item.IsTerminal() && item.m_Applied && !item.m_Published &&
					item.m_DeclaredClass == ParameterEffectClass::Deferred &&
					item.m_Identity.m_Domain == gate.m_EffectDomain &&
					item.m_Identity.m_Generation == gate.m_EffectGeneration &&
					batch->m_Bindings[index]->CoversReadinessGate(item.m_Identity, gate.m_Name))
				{
					return true;
				}
			}
		}
		return false;
	}

	bool SessionParameterControl::IsAdmissionBlocked(ParameterBatchOwner owner) const noexcept
	{
		return m_MutatingParameterBatch || m_QueuedParameterBatch ||
			m_ParameterBatches.size() >= SessionParameterMaxBatches ||
			std::ranges::any_of(m_ParameterBatches, [&](const auto& batch)
			{
				return owner.IsExternal() || batch->m_Owner != owner;
			});
	}

	uint64_t SessionParameterControl::RequestParameters(const ParameterRequest& request,
		std::string& error, ParameterBatchOwner owner) noexcept
	{
		error.clear();
		if (IsAdmissionBlocked(owner))
		{
			error = "Busy: an application, another owner or retained result capacity blocks admission.";
			return 0;
		}
		m_MutatingParameterBatch = true;
		Enable();
		const auto id = AdmitParameterRequest(request, error, owner);
		m_MutatingParameterBatch = false;
		return id;
	}

	ParameterRequest SessionParameterControl::MakeAssignmentRequest(
		std::span<const LabParameterValue> values) const
	{
		ParameterRequest request;
		request.m_Entries.reserve(values.size());
		for (const auto& value : values)
		{
			const auto* parameter = Find(value.m_Id);
			request.m_Entries.push_back({ value.m_Id,
				parameter ? parameter->m_Desc.m_Type : LabParameterType::Bool,
				ParameterOperation::Assign, value.m_Value });
		}
		return request;
	}

	std::optional<ParameterControlStamp> SessionParameterControl::GetDomainControlStamp(
		ParameterControlDomain domain, std::string_view runIdentity) const
	{
		if (!IsAvailable() || !m_WorldLighting || !m_Enabled ||
			(domain != ParameterControlDomain::Lab && domain != ParameterControlDomain::LightingProfile))
		{
			return std::nullopt;
		}
		ParameterControlStamp stamp{
			.m_RunIdentity = std::string(runIdentity),
			.m_ContentIdentity = m_ContentSerial,
			.m_ActivationRevision = m_ActivationRevision,
			.m_Domain = domain,
			.m_WorldRevision = m_Demo->GetWorld().GetLightingControlRevision(),
			.m_EnvironmentRevision = m_WorldLighting->GetLightingControlRevision() };
		if (domain == ParameterControlDomain::Lab)
		{
			const auto* parameters = GetLabParameters();
			if (!m_Lab || !parameters)
			{
				return std::nullopt;
			}
			stamp.m_LabRevision = m_Lab->GetParameterRevision();
			stamp.m_ValuesRevision = parameters->GetControlRevision();
		}
		else if (GetLightingProfiles().empty())
		{
			return std::nullopt;
		}
		return stamp;
	}

	bool SessionParameterControl::CheckDomainControl(
		const ParameterControlStamp& expected, std::string& error) const
	{
		if (expected.m_ContentIdentity != m_ContentSerial ||
			expected.m_ActivationRevision != m_ActivationRevision)
		{
			error = "ExpectedContentChanged: the protected assignment belongs to another activation.";
			return false;
		}
		const auto current = GetDomainControlStamp(expected.m_Domain, expected.m_RunIdentity);
		if (!current)
		{
			error = "ParameterDomainUnavailable: the protected domain is unavailable.";
			return false;
		}
		constexpr auto last = std::numeric_limits<uint64_t>::max();
		if (current->m_ActivationRevision == last ||
			current->m_LabRevision == last || current->m_ValuesRevision == last ||
			current->m_WorldRevision == last || current->m_EnvironmentRevision == last)
		{
			error = "ParameterControlExhausted: the protected domain has exhausted revision capacity.";
			return false;
		}
		if (*current != expected)
		{
			error = "ExpectedParameterControlMismatch: authored domain intent changed; nothing committed.";
			return false;
		}
		return true;
	}

	uint64_t SessionParameterControl::AdmitParameterRequest(const ParameterRequest& request,
		std::string& error, ParameterBatchOwner owner) noexcept
	{
		const bool exact = request.m_Expected.has_value();
		if (request.m_ExpectedDomain &&
			(exact || !CheckDomainControl(*request.m_ExpectedDomain, error)))
		{
			if (exact)
			{
				error = "ParameterRequestInvalid: Host exact states cannot include a Lab/profile guard.";
			}
			return 0;
		}
		if (!IsAvailable() || (exact && !m_Sources->m_View.HasDisplayView()))
		{
			error = exact ?
				"NoParameterSource: ready active content with a display view is required." :
				"A parameter batch requires ready active content and 1-128 items.";
			return 0;
		}
		if (request.m_Entries.empty() || request.m_Entries.size() > SessionParameterMaxItems)
		{
			error = exact ?
				"HostOverrideInvalid: a Host view operation takes 1-128 entries." :
				"A parameter batch requires ready active content and 1-128 items.";
			return 0;
		}
		if (m_NextParameterBatchId == 0 || m_ParameterRevision == std::numeric_limits<uint64_t>::max() ||
			m_HostViewRevision == std::numeric_limits<uint64_t>::max())
		{
			error = "Parameter transaction identity capacity is exhausted.";
			return 0;
		}
		if (exact)
		{
			// Preserve conflict precedence over entry validation, including would-be NoChange.
			if (request.m_Expected->m_ContentIdentity != m_ContentSerial)
			{
				error = "ExpectedContentChanged: the active content is not the expected activation; nothing changed.";
				return 0;
			}
			if (request.m_Expected->m_HostViewRevision != m_HostViewRevision)
			{
				error = "ExpectedHostViewRevisionMismatch: Host view overrides changed after the expected revision; "
					"nothing changed.";
				return 0;
			}
		}
		std::vector<LabParameterValue> values;
		std::vector<HostViewOverrideState> overrides;
		for (size_t index = 0; index < request.m_Entries.size(); ++index)
		{
			const auto& entry = request.m_Entries[index];
			if (request.m_ExpectedDomain)
			{
				const auto* required = request.m_ExpectedDomain->m_Domain == ParameterControlDomain::Lab ?
					static_cast<const ParameterSourceBase*>(&m_Sources->m_Lab) :
					static_cast<const ParameterSourceBase*>(&m_Sources->m_Profiles);
				if (m_Sources->Find(entry.m_Id) != required || entry.m_Operation != ParameterOperation::Assign)
				{
					error = "ParameterDomainUnsupported: protected Assign requires one Lab or lighting-profile source.";
					return 0;
				}
			}
			const bool assign = entry.m_Operation == ParameterOperation::Assign;
			const bool pin = entry.m_Operation == ParameterOperation::Pin;
			const bool remove = entry.m_Operation == ParameterOperation::RemoveOverride;
			if ((!assign && !pin && !remove) || assign == exact ||
				entry.m_Value.has_value() == remove)
			{
				error = "ParameterRequestInvalid: use unguarded Assign values or guarded Host Pin/removal entries.";
				return 0;
			}
			if (exact)
			{
				if (std::ranges::any_of(std::span(request.m_Entries).first(index),
					[&](const auto& previous) { return previous.m_Id == entry.m_Id; }))
				{
					error = "HostOverrideInvalid: Host view override ids must be distinct.";
					return 0;
				}
				const auto* parameter = m_Sources->Find(entry.m_Id) == &m_Sources->m_View ?
					m_Sources->m_View.GetParameters()->Find(entry.m_Id) :
					nullptr;
				if (!parameter || parameter->m_Desc.m_Type != entry.m_Type)
				{
					error = std::format("HostOverrideUnsupported: '{}' is not a Host view parameter of the "
						"requested type; Lab parameters and environment.profile use set.", entry.m_Id.GetName());
					return 0;
				}
				overrides.push_back({ entry.m_Id, entry.m_Type, entry.m_Value });
			}
			if (entry.m_Value)
			{
				values.push_back({ entry.m_Id, *entry.m_Value });
			}
		}
		if (exact)
		{
			LabParameterSet candidate = *m_Sources->m_View.GetParameters();
			if (!values.empty() && !candidate.ApplyBatch(values, error))
			{
				error = "HostOverrideInvalid: " + error;
				return 0;
			}
		}
		else
		{
			if (!ValidateParameterBatch(values, error))
			{
				return 0;
			}
			for (const auto& entry : request.m_Entries)
			{
				if (Find(entry.m_Id)->m_Desc.m_Type != entry.m_Type)
				{
					error = "ParameterRequestInvalid: the declared Assign type differs from its owning descriptor.";
					return 0;
				}
			}
		}
		std::vector<ParameterSourceBase*> bindings;
		bool hasHost = false;
		bool hasLab = false;
		for (const auto& entry : request.m_Entries)
		{
			auto* source = m_Sources->Find(entry.m_Id);
			bindings.push_back(source);
			hasHost |= source->IsHost();
			hasLab |= !source->IsHost();
		}
		const uint64_t id = m_NextParameterBatchId;
		std::vector<ParameterSourceBase*> reserved;
		if (!exact)
		{
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
		}
		++m_NextParameterBatchId;
		m_ParameterBatches.push_back(std::make_unique<ParameterBatch>(request));
		auto& batch = *m_ParameterBatches.back();
		batch.m_Owner = owner;
		m_QueuedParameterBatch = id;
		batch.m_Status.m_BatchId = id;
		batch.m_Status.m_ContentId = m_ContentId;
		batch.m_Status.m_Source = hasHost ? (hasLab ? "mixed" : "host") : "lab";
		batch.m_Session = m_ContentSerial;
		if (request.m_ExpectedDomain)
		{
			batch.m_Status.m_DomainOperation = ParameterControlOperation{ .m_Expected = *request.m_ExpectedDomain };
		}
		if (exact)
		{
			batch.m_Status.m_HostViewOperation = HostViewOverrideOperation{
				.m_Expected = *request.m_Expected,
				.m_Overrides = std::move(overrides),
			};
		}
		else
		{
			batch.m_Status.m_Values = std::move(values);
		}
		batch.m_Bindings = std::move(bindings);
		return id;
	}

	HostViewControlStamp SessionParameterControl::MakeHostViewStamp(std::string_view runIdentity) const
	{
		return {
			.m_RunIdentity = std::string(runIdentity),
			.m_ContentIdentity = m_ContentSerial,
			.m_HostViewRevision = m_HostViewRevision,
		};
	}

	const ParameterBatchStatus* SessionParameterControl::GetParameterBatchStatus() const noexcept
	{
		const auto found = std::ranges::find_if(m_ParameterBatches,
			[](const auto& batch) { return batch->m_Owner.IsExternal(); });
		return found != m_ParameterBatches.end() ? &(*found)->m_Status : nullptr;
	}

	const ParameterBatchStatus* SessionParameterControl::GetParameterBatchStatus(
		uint64_t id, ParameterBatchOwner owner) const noexcept
	{
		const auto found = std::ranges::find_if(m_ParameterBatches, [&](const auto& batch)
		{
			return batch->m_Status.m_BatchId == id && batch->m_Owner == owner;
		});
		return found != m_ParameterBatches.end() ? &(*found)->m_Status : nullptr;
	}

	std::vector<ParameterBatchStatus> SessionParameterControl::GetParameterBatchStatuses(
		ParameterBatchOwner owner) const
	{
		std::vector<ParameterBatchStatus> result;
		for (const auto& batch : m_ParameterBatches)
		{
			if (batch->m_Owner == owner)
			{
				result.push_back(batch->m_Status);
			}
		}
		return result;
	}

	bool SessionParameterControl::HasPendingParameterBatches() const noexcept
	{
		return std::ranges::any_of(m_ParameterBatches,
			[](const auto& batch) { return !batch->m_Status.IsTerminal(); });
	}

	std::optional<ParameterBatchStatus> SessionParameterControl::TakeParameterBatchResult(
		uint64_t id, ParameterBatchOwner owner) noexcept
	{
		if (m_MutatingParameterBatch)
		{
			return std::nullopt;
		}
		const auto found = std::ranges::find_if(m_ParameterBatches, [&](const auto& batch)
		{
			return batch->m_Status.m_BatchId == id && batch->m_Owner == owner && batch->m_Status.IsTerminal();
		});
		if (found == m_ParameterBatches.end())
		{
			return std::nullopt;
		}
		auto result = std::move((*found)->m_Status);
		m_ParameterBatches.erase(found);
		return result;
	}

	void SessionParameterControl::FinishParameterBatch(uint64_t id, ParameterBatchOwner owner) noexcept
	{
		if (!m_MutatingParameterBatch)
		{
			for (auto& batch : m_ParameterBatches)
			{
				if (batch->m_Status.m_BatchId == id && batch->m_Owner == owner)
				{
					EndParameterBatch(*batch, false, "The owner ended before parameter consumption.");
					return;
				}
			}
		}
	}

	void SessionParameterControl::FinishParameterBatches(ParameterBatchOwner owner) noexcept
	{
		if (!m_MutatingParameterBatch)
		{
			for (auto& batch : m_ParameterBatches)
			{
				if (batch->m_Owner == owner)
				{
					EndParameterBatch(*batch, false, "The owner ended before parameter consumption.");
				}
			}
		}
	}

	void SessionParameterControl::RefreshParameterBatchStatus(ParameterBatch& batch) noexcept
	{
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

	void SessionParameterControl::EndParameterBatch(ParameterBatch& batch,
		bool superseded, std::string reason) noexcept
	{
		if (batch.m_Status.IsTerminal())
		{
			return;
		}
		if (m_QueuedParameterBatch == batch.m_Status.m_BatchId)
		{
			for (auto* source : m_Sources->Ordered())
			{
				source->Cancel(batch.m_Status.m_BatchId);
			}
			m_QueuedParameterBatch = 0;
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
		RefreshParameterBatchStatus(batch);
	}

	void SessionParameterControl::EndHostViewOperation(ParameterBatch& batch,
		std::string_view conflict, std::string_view reason) noexcept
	{
		GGLAB_ASSERT(batch.m_Status.m_HostViewOperation && batch.m_Receipts.empty());
		batch.m_Status.m_HostViewOperation->m_Conflict = conflict;
		EndParameterBatch(batch, false, std::format("{}: {}", conflict, reason));
	}

	void SessionParameterControl::EndForContentChange(ParameterBatch& batch, std::string reason) noexcept
	{
		if (batch.m_Request.m_ExpectedDomain && batch.m_Receipts.empty() && !batch.m_Status.IsTerminal())
		{
			EndDomainOperation(batch, "ExpectedContentChanged: content changed before protected assignment committed.");
			return;
		}
		// An unapplied operation was prepared against the replaced activation.
		if (batch.m_Request.m_Expected && batch.m_Receipts.empty() && !batch.m_Status.IsTerminal())
		{
			EndHostViewOperation(batch, "ExpectedContentChanged",
				"the content changed before the operation applied; nothing changed.");
			return;
		}
		EndParameterBatch(batch, true, std::move(reason));
	}

	void SessionParameterControl::EndDomainOperation(ParameterBatch& batch, std::string error) noexcept
	{
		GGLAB_ASSERT(batch.m_Status.m_DomainOperation && batch.m_Receipts.empty());
		batch.m_Status.m_DomainOperation->m_Conflict = error.substr(0, error.find(':'));
		EndParameterBatch(batch, false, std::move(error));
	}

	void SessionParameterControl::OnRuntimeFailed(std::string_view reason) noexcept
	{
		m_ParameterIntakeFailed = true;
		for (auto& entry : m_ParameterBatches)
		{
			auto& batch = *entry;
			if (batch.m_Status.IsTerminal())
			{
				continue;
			}
			if (m_QueuedParameterBatch == batch.m_Status.m_BatchId)
			{
				for (auto* source : m_Sources->Ordered())
				{
					source->Cancel(batch.m_Status.m_BatchId);
				}
				m_QueuedParameterBatch = 0;
			}
			for (auto& receipt : batch.m_Receipts)
			{
				GGLAB_UNUSED(receipt.Fail(std::string(reason)));
			}
			batch.m_Consumed.clear();
			RefreshParameterBatchStatus(batch);
			batch.m_Status.m_State = ParameterBatchState::Failed;
			batch.m_Status.m_Failure = reason;
		}
	}

	void SessionParameterControl::Finish() noexcept
	{
		for (auto& batch : m_ParameterBatches)
		{
			EndParameterBatch(*batch, false, "The runtime shut down before parameter consumption.");
		}
		m_ParameterIntakeFailed = true;
		const bool discardsHostView = !GetHostViewOverrides().empty();
		m_Sources->Bind(nullptr, nullptr, 0);
		if (discardsHostView)
		{
			AdvanceHostViewRevision();
		}
		m_Demo = nullptr;
		m_Lab = nullptr;
	}

	void SessionParameterControl::OnFrameReady(uint64_t serial) noexcept
	{
		if (serial == 0 || serial <= m_LastReadyFrameSerial || m_MutatingParameterBatch)
		{
			return;
		}
		m_LastReadyFrameSerial = serial;
		if (!HasPendingParameterBatches())
		{
			return;
		}
		m_MutatingParameterBatch = true;
		m_Sources->m_Profiles.Synchronize();
		RefreshSources();
		m_Sources->m_Domain.BeginFrame();
		for (auto& batch : m_ParameterBatches)
		{
			if (!batch->m_Status.IsTerminal())
			{
				batch->m_Consumed.clear();
				batch->m_FrameSerial = serial;
				if (batch->m_Session != m_ContentSerial)
				{
					EndForContentChange(*batch, "The content changed before parameter application.");
				}
			}
		}
		if (m_QueuedParameterBatch)
		{
			const auto queued = std::ranges::find_if(m_ParameterBatches,
				[&](const auto& batch) { return batch->m_Status.m_BatchId == m_QueuedParameterBatch; });
			GGLAB_ASSERT(queued != m_ParameterBatches.end());
			ApplyParameterBatch(**queued, serial);
		}
		m_MutatingParameterBatch = false;
	}

	void SessionParameterControl::ApplyParameterBatch(ParameterBatch& batch, uint64_t serial) noexcept
	{
		m_Sources->m_Domain.BeginApplication(batch.m_Domain);
		if (!IsAvailable() || m_ParameterRevision == std::numeric_limits<uint64_t>::max() ||
			m_HostViewRevision == std::numeric_limits<uint64_t>::max())
		{
			EndParameterBatch(batch, false, "The content is unavailable at the safe parameter boundary.");
			return;
		}
		if (!m_Sources->m_View.HasDisplayView())
		{
			EndParameterBatch(batch, false, "ParameterPreparationFailed: the display view is unavailable.");
			return;
		}
		const auto& request = batch.m_Request;
		if (request.m_ExpectedDomain)
		{
			auto& control = *batch.m_Status.m_DomainOperation;
			control.m_Before = GetDomainControlStamp(request.m_ExpectedDomain->m_Domain,
				request.m_ExpectedDomain->m_RunIdentity);
			std::string conflict;
			if (!CheckDomainControl(*request.m_ExpectedDomain, conflict))
			{
				EndDomainOperation(batch, std::move(conflict));
				return;
			}
		}
		const auto& expected = request.m_Expected;
		auto& operation = batch.m_Status.m_HostViewOperation;
		if (expected)
		{
			// The stamp is checked again where it applies; a stale operation applies nothing.
			operation->m_Before = MakeHostViewStamp(expected->m_RunIdentity);
			if (expected->m_ContentIdentity != m_ContentSerial)
			{
				EndHostViewOperation(batch, "ExpectedContentChanged",
					"the active content is not the expected activation; nothing changed.");
				return;
			}
			if (expected->m_HostViewRevision != m_HostViewRevision)
			{
				EndHostViewOperation(batch, "ExpectedHostViewRevisionMismatch",
					"Host view overrides changed after the expected revision; nothing changed.");
				return;
			}
		}
		struct SourcePlan
		{
			ParameterSourceBase* m_Source = nullptr;
			std::unique_ptr<PreparedParameterSourceBase> m_Prepared;
		};
		std::vector<SourcePlan> plans;
		plans.reserve(m_Sources->Ordered().size());
		size_t deferredItems = 0;
		for (size_t index = 0; index < batch.m_Bindings.size(); ++index)
		{
			const auto& id = batch.m_Request.m_Entries[index].m_Id;
			const auto* current = Find(id);
			if (!current || current->m_Desc.m_Type != request.m_Entries[index].m_Type ||
				batch.m_Bindings[index] != m_Sources->Find(id) ||
				!SupportsParameterEffect(current->m_Desc))
			{
				EndParameterBatch(batch, false, "ParameterPreparationFailed: a source or supported effect changed.");
				return;
			}
			deferredItems += GetParameterEffectClass(current->m_Desc) == ParameterEffectClass::Deferred;
			if (deferredItems > 1)
			{
				EndParameterBatch(batch, false, "ParameterPreparationFailed: at most one bound lighting item is supported.");
				return;
			}
		}
		std::string error;
		bool changed = false;
		std::vector<LabParameterValue> targets;
		if (expected)
		{
			std::vector<HostViewOverrideState> overrides;
			for (const auto& entry : request.m_Entries)
			{
				overrides.push_back({ entry.m_Id, entry.m_Type, entry.m_Value });
			}
			auto prepared = m_Sources->m_View.PrepareOverrides(overrides, targets, error);
			if (!prepared)
			{
				EndParameterBatch(batch, false, std::move(error));
				return;
			}
			changed = prepared->HasChanges();
			plans.push_back({ &m_Sources->m_View, std::move(prepared) });
		}
		else
		{
			// Prepare every owning source before consuming reservations or mutating state.
			for (auto* source : m_Sources->Ordered())
			{
				std::vector<LabParameterValue> values;
				for (size_t index = 0; index < request.m_Entries.size(); ++index)
				{
					if (batch.m_Bindings[index] == source)
					{
						values.push_back({ request.m_Entries[index].m_Id, *request.m_Entries[index].m_Value });
					}
				}
				if (values.empty())
				{
					continue;
				}
				auto prepared = source->Prepare(batch.m_Status.m_BatchId, values, error);
				if (!prepared)
				{
					EndParameterBatch(batch, false, std::move(error));
					return;
				}
				changed |= prepared->HasChanges();
				plans.push_back({ source, std::move(prepared) });
			}
		}
		const uint64_t revision = m_ParameterRevision + (changed ? 1 : 0);
		std::vector<ParameterEffectReceipt> receipts;
		receipts.reserve(batch.m_Bindings.size());
		std::vector<bool> noChange(batch.m_Bindings.size(), false);
		for (size_t index = 0; index < batch.m_Bindings.size(); ++index)
		{
			const auto& id = batch.m_Request.m_Entries[index].m_Id;
			auto receipt = ParameterEffectReceipt::Begin(id,
				GetParameterEffectClass(Find(id)->m_Desc), revision, serial);
			if (!receipt)
			{
				EndParameterBatch(batch, false, "ParameterPreparationFailed: receipt identity is unavailable.");
				return;
			}
			receipts.push_back(std::move(*receipt));
		}

		const std::vector<LabParameterValue> hostViewBefore(
			GetHostViewOverrides().begin(), GetHostViewOverrides().end());
		if (request.m_ExpectedDomain && !CheckDomainControl(*request.m_ExpectedDomain, error))
		{
			EndDomainOperation(batch, std::move(error));
			return;
		}
		// No callbacks or rejection in this region. Install every source and the
		// unique receipt owner before allowing any content handler to observe state.
		for (auto& plan : plans)
		{
			plan.m_Prepared->Commit();
		}
		if (expected)
		{
			// Items prove the resolved targets, including inheritance resolved for removals.
			batch.m_Status.m_Values = std::move(targets);
		}
		m_QueuedParameterBatch = 0;
		if (batch.m_Status.m_DomainOperation)
		{
			batch.m_Status.m_DomainOperation->m_Committed = true;
		}
		const auto committedControl = request.m_ExpectedDomain ?
			GetDomainControlStamp(request.m_ExpectedDomain->m_Domain, request.m_ExpectedDomain->m_RunIdentity) :
			std::nullopt;
		m_Sources->PublishHostParameters();
		m_ParameterRevision = revision;
		m_ObservedLabRevision = m_Lab ? m_Lab->GetParameterRevision() : 0;
		batch.m_Receipts = std::move(receipts);
		batch.m_NoChange = std::move(noChange);
		batch.m_Status.m_Revision = revision;
		batch.m_Status.m_State = ParameterBatchState::Applying;
		RefreshParameterBatchStatus(batch);
		SupersedeOverwrittenItems(batch);

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
		// Finalization has removed newly introduced equal overrides, so this compares
		// the committed override set rather than the requested values.
		if (!SameOverrides(hostViewBefore, GetHostViewOverrides()))
		{
			AdvanceHostViewRevision();
		}
		if (expected)
		{
			operation->m_After = MakeHostViewStamp(expected->m_RunIdentity);
			for (const auto& state : request.m_Entries)
			{
				auto before = FindOverride(hostViewBefore, state.m_Id);
				auto after = FindOverride(GetHostViewOverrides(), state.m_Id);
				if (before.has_value() != after.has_value() || (before && !LabValuesEqual(*before, *after)))
				{
					operation->m_Changes.push_back({ state.m_Id, std::move(before), std::move(after) });
				}
			}
		}
		RefreshSources();
		m_ParameterRevision = revision;
		if (request.m_ExpectedDomain)
		{
			const auto current = GetDomainControlStamp(
				request.m_ExpectedDomain->m_Domain, request.m_ExpectedDomain->m_RunIdentity);
			batch.m_Status.m_DomainOperation->m_After = current ? current : committedControl;
		}
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
				batch.m_Rebuilding |= application.m_ActualClass == ParameterEffectClass::Rebuild;
				batch.m_WaitingForContent = batch.m_Rebuilding;
				if (application.m_ActualClass == ParameterEffectClass::Rebuild)
				{
					batch.m_Status.m_Impact = std::max(batch.m_Status.m_Impact,
						Find(receipt.GetSnapshot().m_Parameter)->m_Desc.m_Impact);
				}
			}
		}
		m_Sources->m_Domain.ObserveFrameEffects(batch.m_Domain);
		RefreshParameterBatchStatus(batch);
	}

	void SessionParameterControl::SupersedeOverwrittenItems(const ParameterBatch& applied) noexcept
	{
		for (auto& entry : m_ParameterBatches)
		{
			auto& batch = *entry;
			if (&batch == &applied || batch.m_Status.IsTerminal())
			{
				continue;
			}
			for (size_t index = 0; index < batch.m_Receipts.size(); ++index)
			{
				const auto& previous = batch.m_Status.m_Values[index];
				const auto found = std::ranges::find(applied.m_Status.m_Values, previous.m_Id, &LabParameterValue::m_Id);
				if (found != applied.m_Status.m_Values.end() && !LabValuesEqual(found->m_Value, previous.m_Value))
				{
					GGLAB_UNUSED(batch.m_Receipts[index].Supersede());
				}
			}
			RefreshParameterBatchStatus(batch);
		}
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
		if (!m_Demo)
		{
			return;
		}
		const ParameterSourceFrame frame{ settings, view, lightingGeneration, m_ContentSerial };
		for (auto& entry : m_ParameterBatches)
		{
			auto& batch = *entry;
			if (batch.m_Status.IsTerminal() || serial != batch.m_FrameSerial)
			{
				continue;
			}
			batch.m_Consumed.clear();
			for (size_t index = 0; index < batch.m_Receipts.size(); ++index)
			{
				auto& receipt = batch.m_Receipts[index];
				if (receipt.GetSnapshot().IsTerminal())
				{
					continue;
				}
				const auto& requested = batch.m_Status.m_Values[index];
				auto* source = batch.m_Bindings[index];
				if (batch.m_WaitingForContent && !source->IsHost())
				{
					// The candidate owns Lab inputs; old-session frames cannot prove them.
					continue;
				}
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
				if (receipt.GetSnapshot().m_DeclaredClass != ParameterEffectClass::Immediate)
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
				auto application = batch.m_Rebuilding ? ParameterApplication{} :
					m_Sources->m_Domain.ObserveImmediate(batch.m_Domain);
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
			RefreshParameterBatchStatus(batch);
		}
	}

	ParameterFrameState SessionParameterControl::CaptureFrameState(uint64_t serial,
		uint64_t requestedLightingGeneration, uint64_t activeLightingGeneration) const
	{
		ParameterFrameState frame{ .m_FrameSerial = serial, .m_Revision = m_ParameterRevision,
			.m_ContentId = m_ContentId, .m_Available = IsEnabled() && IsAvailable(),
			.m_RequestedLighting = { requestedLightingGeneration },
			.m_ActiveLighting = { activeLightingGeneration } };
		const auto& publication = GetLightingProfilePublication();
		if (publication.m_Requested && publication.m_Requested->m_Generation == requestedLightingGeneration)
		{
			frame.m_RequestedLighting.m_Profile = publication.m_Requested;
		}
		if (publication.m_Active && publication.m_Active->m_Generation == activeLightingGeneration)
		{
			frame.m_ActiveLighting.m_Profile = publication.m_Active;
		}
		// Edit availability can close during candidate preparation or warmup. The
		// owning sources still provide requested inputs for those transition frames.
		if (m_Enabled && m_Demo)
		{
			for (const auto* source : m_Sources->Ordered())
			{
				const auto* parameters = source->GetParameters();
				if (!parameters)
				{
					continue;
				}
				for (const auto& value : parameters->GetParameters())
				{
					if (!LabValuesEqual(value.m_Value, value.m_Desc.m_DefaultValue))
					{
						frame.m_Values.push_back({ std::string(value.m_Desc.m_Id.GetName()),
							source->IsHost() ? "host" : "lab",
							value.m_Desc.m_Type, value.m_Value, value.m_Desc.m_DefaultValue });
					}
				}
			}
		}
		for (const auto& batch : m_ParameterBatches)
		{
			if (batch->m_FrameSerial == serial)
			{
				for (const auto& receipt : batch->m_Receipts)
				{
					frame.m_Receipts.push_back({ batch->m_Status.m_BatchId, receipt.GetSnapshot() });
				}
			}
		}
		return frame;
	}

	void SessionParameterControl::CompleteFrameState(ParameterFrameState& frame) const noexcept
	{
		for (auto& receipt : frame.m_Receipts)
		{
			for (const auto& batch : m_ParameterBatches)
			{
				if (batch->m_Status.m_BatchId != receipt.m_BatchId || batch->m_FrameSerial != frame.m_FrameSerial)
				{
					continue;
				}
				for (const auto& current : batch->m_Receipts)
				{
					const auto& item = current.GetSnapshot();
					if (item.m_Parameter == receipt.m_Item.m_Parameter && item.m_Identity == receipt.m_Item.m_Identity &&
						item.m_FirstConsumed == frame.m_FrameSerial)
					{
						receipt.m_Item = item;
					}
				}
			}
		}
	}

	void SessionParameterControl::OnFrameSubmitted(const DemoFrameFeedback& feedback) noexcept
	{
		for (auto& entry : m_ParameterBatches)
		{
			auto& batch = *entry;
			if (batch.m_Status.IsTerminal() || feedback.m_RenderFrameSerial != batch.m_FrameSerial)
			{
				continue;
			}
			if (feedback.m_RenderSceneStatus == RenderSceneBuildStatus::Ready && feedback.m_SubmittedFence.IsValid())
			{
				for (const auto& consumed : batch.m_Consumed)
				{
					GGLAB_UNUSED(batch.m_Receipts[consumed.m_Item].RecordSubmittedFrame(
						consumed.m_Identity, feedback.m_RenderFrameSerial));
				}
			}
			batch.m_Consumed.clear();
			RefreshParameterBatchStatus(batch);
		}
	}
}
