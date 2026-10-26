#include "Application/Control/ApplicationControlParameters.h"
#include "Capture/RequiredFrameState.h"
#include "GGLabFoundation/Hash/Sha256.h"
#include "GGLabRuntime/Graphics/LightingProfile.h"
#include "Application/Control/ApplicationControlProtocol.h"
#include "Parameters/HostViewParameters.h"
#include "Parameters/ParameterRequest.h"
#include "Parameters/SessionParameterControl.h"
#include "GGLabAppRuntime.h"
#include "GGLabFoundation/Base/CoreMacros.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <limits>
#include <memory>
#include <optional>
#include <string_view>
#include <type_traits>
#include <variant>
#include <utility>

namespace gglab
{
	static_assert(ApplicationControlMaxParameterItems == SessionParameterMaxItems);
	namespace
	{
		using Json = nlohmann::json;
		constexpr std::array<std::string_view, 7> ParameterTypes =
		{
			"bool", "int", "uint", "float", "enum", "vector3", "color",
		};

		[[nodiscard]] Json Response(uint64_t id, bool ok)
		{
			return { { "protocol", ApplicationControlProtocolVersion }, { "id", id }, { "ok", ok } };
		}

		[[nodiscard]] std::string Dump(const Json& value)
		{
			return value.dump(-1, ' ', false, Json::error_handler_t::replace);
		}

		[[nodiscard]] Json Value(const LabValue& value)
		{
			return std::visit([](const auto& item) -> Json
			{
				using T = std::decay_t<decltype(item)>;
				if constexpr (std::is_same_v<T, Vector3>)
				{
					return Json::array({ item.m_X, item.m_Y, item.m_Z });
				}
				else if constexpr (std::is_same_v<T, Color>)
				{
					return Json::array({ item.R(), item.G(), item.B(), item.A() });
				}
				else
				{
					return item;
				}
			}, value);
		}

		[[nodiscard]] Json HostOverride(const std::optional<LabValue>& value)
		{
			return value ?
				Json{ { "present", true }, { "value", Value(*value) } } :
				Json{ { "present", false } };
		}

		[[nodiscard]] Json ErrorCode(std::string_view error)
		{
			const auto code = error.substr(0, error.find(':'));
			return std::ranges::find(ApplicationControlHostViewOverrideErrorCodes, code) !=
				ApplicationControlHostViewOverrideErrorCodes.end() ?
					Json(std::string(code)) :
					Json();
		}

		[[nodiscard]] const char* EffectClass(ParameterEffectClass value)
		{
			switch (value)
			{
			case ParameterEffectClass::Immediate:
				return "Immediate";
			case ParameterEffectClass::Deferred:
				return "Deferred";
			default:
				return "Rebuild";
			}
		}

		[[nodiscard]] const char* EffectState(ParameterEffectState value)
		{
			switch (value)
			{
			case ParameterEffectState::Requested:
				return "Requested";
			case ParameterEffectState::Applied:
				return "Applied";
			case ParameterEffectState::Published:
				return "Published";
			case ParameterEffectState::Consumed:
				return "Consumed";
			case ParameterEffectState::Superseded:
				return "Superseded";
			case ParameterEffectState::NoChange:
				return "NoChange";
			case ParameterEffectState::EffectNotReached:
				return "EffectNotReached";
			default:
				return "Failed";
			}
		}

		[[nodiscard]] const char* Impact(LabChangeImpact value)
		{
			switch (value)
			{
			case LabChangeImpact::Immediate:
				return "Immediate";
			case LabChangeImpact::RebuildScene:
				return "RebuildScene";
			case LabChangeImpact::RecreatePipeline:
				return "RecreatePipeline";
			default:
				return "RestartSession";
			}
		}

		[[nodiscard]] bool FloatValue(const Json& field, float& result)
		{
			if (!field.is_number())
			{
				return false;
			}
			const double number = field.get<double>();
			if (!std::isfinite(number) || std::abs(number) > std::numeric_limits<float>::max())
			{
				return false;
			}
			result = static_cast<float>(number);
			return true;
		}

		[[nodiscard]] std::optional<LabValue> ParseValue(LabParameterType type, const Json& field)
		{
			switch (type)
			{
			case LabParameterType::Bool:
				if (field.is_boolean())
				{
					return field.get<bool>();
				}
				break;
			case LabParameterType::Int:
			case LabParameterType::Enum:
				if (field.is_number_integer() &&
					(field.is_number_unsigned() ? field.get<uint64_t>() <= INT32_MAX :
						field.get<int64_t>() >= INT32_MIN && field.get<int64_t>() <= INT32_MAX))
				{
					return field.get<int32_t>();
				}
				break;
			case LabParameterType::UInt:
				if (field.is_number_unsigned() && field.get<uint64_t>() <= UINT32_MAX)
				{
					return field.get<uint32_t>();
				}
				break;
			case LabParameterType::Float:
			{
				float number = 0.0f;
				if (FloatValue(field, number))
				{
					return number;
				}
				break;
			}
			case LabParameterType::Vector3:
			case LabParameterType::Color:
			{
				const size_t count = type == LabParameterType::Vector3 ? 3 : 4;
				std::array<float, 4> components{};
				if (!field.is_array() || field.size() != count)
				{
					break;
				}
				for (size_t i = 0; i < count; ++i)
				{
					if (!FloatValue(field[i], components[i]))
					{
						return std::nullopt;
					}
				}
				if (type == LabParameterType::Vector3)
				{
					return Vector3(components[0], components[1], components[2]);
				}
				return Color(components[0], components[1], components[2], components[3]);
			}
			}
			return std::nullopt;
		}
	}

	std::string ParseApplicationControlParameterChanges(
		const Json& changes, std::vector<ApplicationControlParameterChange>& result)
	{
		if (!changes.is_array() || changes.empty() || changes.size() > ApplicationControlMaxParameterItems)
		{
			return "Field 'changes' must contain 1 to 128 typed parameter changes.";
		}
		std::vector<ApplicationControlParameterChange> candidate;
		for (const auto& item : changes)
		{
			if (!item.is_object() || item.size() != 3 || !item.contains("id") ||
				!item["id"].is_string() || !item.contains("type") || !item["type"].is_string() ||
				!item.contains("value"))
			{
				return "Each parameter change takes exactly 'id', 'type' and 'value'.";
			}
			const std::string name = item["id"].get<std::string>();
			const auto type = std::ranges::find(ParameterTypes, item["type"].get<std::string>());
			if (name.empty() || name.size() > 128 || name.find('\0') != std::string::npos ||
				type == ParameterTypes.end() || std::ranges::any_of(candidate, [&name](const auto& change)
				{
					return change.m_Value.m_Id.GetName() == name;
				}))
			{
				return "Parameter ids must be distinct non-empty strings (at most 128 UTF-8 bytes); types must be declared parameter types.";
			}
			const auto parameterType = static_cast<LabParameterType>(type - ParameterTypes.begin());
			const auto value = ParseValue(parameterType, item["value"]);
			if (!value)
			{
				return "Parameter value does not match its type or exceeds finite 32-bit storage; strings and numeric coercion to integers/bools are not supported.";
			}
			candidate.push_back({ parameterType, { LabParameterId(name), *value } });
		}
		result = std::move(candidate);
		return {};
	}

	std::string ParseApplicationControlParameterAssignments(
		const Json& assignments, std::vector<LabParameterValue>& result)
	{
		if (!assignments.is_object() || assignments.empty() ||
			assignments.size() > ApplicationControlMaxParameterItems)
		{
			return "Event 'set' must be an object with 1-128 parameter assignments.";
		}
		Json changes = Json::array();
		for (const auto& [id, value] : assignments.items())
		{
			const char* type = value.is_boolean() ? "bool" :
				value.is_number_unsigned() ? "uint" :
					value.is_number_integer() ? "int" :
						value.is_number_float() ? "float" :
							value.is_array() && value.size() == 3 ? "vector3" :
								value.is_array() && value.size() == 4 ? "color" : "invalid";
			changes.push_back({ { "id", id }, { "type", type }, { "value", value } });
		}
		std::vector<ApplicationControlParameterChange> parsed;
		if (auto error = ParseApplicationControlParameterChanges(changes, parsed); !error.empty())
		{
			return error;
		}
		for (auto& change : parsed)
		{
			result.push_back(std::move(change.m_Value));
		}
		return {};
	}

	std::string ParseApplicationControlHostViewOverrides(
		const Json& overrides, std::vector<HostViewOverrideState>& result)
	{
		if (!overrides.is_array() || overrides.empty() || overrides.size() > ApplicationControlMaxParameterItems)
		{
			return "Field 'overrides' must contain 1 to 128 typed Host view override states.";
		}
		std::vector<HostViewOverrideState> candidate;
		for (const auto& item : overrides)
		{
			const auto presence = item.find("present");
			const bool present = presence != item.end() && presence->is_boolean() && presence->get<bool>();
			if (!item.is_object() || presence == item.end() || !presence->is_boolean() ||
				item.size() != (present ? 4u : 3u) || item.contains("value") != present ||
				!item.contains("id") || !item["id"].is_string() || !item.contains("type") || !item["type"].is_string())
			{
				return "Each override state takes 'id', 'type' and boolean 'present', plus 'value' exactly when present.";
			}
			const std::string name = item["id"].get<std::string>();
			const auto type = std::ranges::find(ParameterTypes, item["type"].get<std::string>());
			if (name.empty() || name.size() > 128 || name.find('\0') != std::string::npos ||
				type == ParameterTypes.end() || std::ranges::any_of(candidate, [&name](const auto& state)
				{
					return state.m_Id.GetName() == name;
				}))
			{
				return "Override ids must be distinct non-empty strings (at most 128 UTF-8 bytes); types must be declared parameter types.";
			}
			HostViewOverrideState state{
				.m_Id = LabParameterId(name),
				.m_Type = static_cast<LabParameterType>(type - ParameterTypes.begin()),
			};
			if (present)
			{
				state.m_Value = ParseValue(state.m_Type, item["value"]);
				if (!state.m_Value)
				{
					return "Override value does not match its type or exceeds finite 32-bit storage; strings and numeric coercion to integers/bools are not supported.";
				}
			}
			candidate.push_back(std::move(state));
		}
		result = std::move(candidate);
		return {};
	}

	std::string ParseApplicationControlRequiredFrameState(
		const Json& document, std::shared_ptr<const RequiredFrameState>& result)
	{
		constexpr std::array<std::string_view, 6> Keys = { "runIdentity", "contentIdentity", "minHostViewRevision",
			"values", "temporalConsumers", "referenceSampling" };
		if (!document.is_object())
		{
			return "Field 'requiredFrameState' must be an object.";
		}
		for (const auto& [key, value] : document.items())
		{
			if (std::ranges::find(Keys, key) == Keys.end())
			{
				return std::format("Unknown field '{}' in 'requiredFrameState'.", key);
			}
		}
		const auto run = document.find("runIdentity");
		const auto content = document.find("contentIdentity");
		const auto revision = document.find("minHostViewRevision");
		const auto values = document.find("values");
		const bool runIdentity = run != document.end() && run->is_string() &&
			run->get_ref<const std::string&>().size() == 32 &&
			std::ranges::all_of(run->get_ref<const std::string&>(), [](char digit)
			{
				return (digit >= '0' && digit <= '9') || (digit >= 'a' && digit <= 'f');
			});
		if (!runIdentity || content == document.end() || !content->is_number_unsigned() ||
			revision == document.end() || !revision->is_number_unsigned() || values == document.end() ||
			!values->is_array() || values->empty() || values->size() > RequiredFrameStateMaxValues)
		{
			return "Field 'requiredFrameState' requires 'runIdentity' (32 lowercase hexadecimal digits), unsigned "
				"'contentIdentity' and 'minHostViewRevision', and 1 to 128 'values'.";
		}
		auto state = std::make_shared<RequiredFrameState>();
		state->m_RunIdentity = run->get<std::string>();
		state->m_ContentIdentity = content->get<uint64_t>();
		state->m_MinHostViewRevision = revision->get<uint64_t>();
		for (const auto& item : *values)
		{
			if (!item.is_object() || !item.contains("id") || !item["id"].is_string() || !item.contains("type") ||
				!item["type"].is_string() || !item.contains("value") ||
				item.size() != (item.contains("source") ? 4u : 3u))
			{
				return "Each required value takes 'id', 'type', 'value' and an optional 'source'.";
			}
			const auto type = std::ranges::find(ParameterTypes, item["type"].get<std::string>());
			if (type == ParameterTypes.end())
			{
				return "Required value types must be declared parameter types.";
			}
			RequiredFrameValue required{
				.m_Id = LabParameterId(item["id"].get<std::string>()),
				.m_Type = static_cast<LabParameterType>(type - ParameterTypes.begin()),
			};
			const auto value = ParseValue(required.m_Type, item["value"]);
			if (!value)
			{
				return "Required value does not match its type or exceeds finite 32-bit storage.";
			}
			required.m_Value = *value;
			if (item.contains("source"))
			{
				required.m_Source = item["source"].is_string() ?
					FindHostViewValueSource(item["source"].get<std::string>()) :
					std::nullopt;
				if (!required.m_Source)
				{
					return "Field 'source' must be 'content', 'tooling', 'camera', 'host', 'sequence' or "
						"'forced-reference'.";
				}
			}
			state->m_Values.push_back(std::move(required));
		}
		if (const auto consumers = document.find("temporalConsumers"); consumers != document.end())
		{
			if (!consumers->is_array() || consumers->size() > TemporalConsumerCount)
			{
				return "Field 'temporalConsumers' takes at most one entry per temporal consumer.";
			}
			for (const auto& item : *consumers)
			{
				const auto name = [&](const char* key) -> std::string
					{
						return item.is_object() && item.contains(key) && item[key].is_string() ?
							item[key].get<std::string>() :
							std::string();
					};
				const auto consumer = FindTemporalConsumer(name("consumer"));
				const auto status = FindTemporalConsumerStatus(name("status"));
				const bool hasReason = item.is_object() && item.contains("disableReason");
				const auto reason = FindTemporalConsumerDisableReason(name("disableReason"));
				if (!item.is_object() || item.size() != (hasReason ? 3u : 2u) || !consumer || !status ||
					(hasReason && !reason))
				{
					return "Each required temporal consumer takes the 'consumer', 'status' and optional "
						"'disableReason' names that status.frame reports.";
				}
				state->m_TemporalConsumers.push_back({ *consumer, *status, hasReason ? reason : std::nullopt });
			}
		}
		if (const auto reference = document.find("referenceSampling"); reference != document.end())
		{
			if (!reference->is_boolean())
			{
				return "Field 'referenceSampling' must be a boolean.";
			}
			state->m_ReferenceSampling = reference->get<bool>();
		}
		result = std::move(state);
		return {};
	}

	Json SerializeApplicationControlParameterBatch(const ParameterBatchStatus& batch)
	{
		const char* state = "Queued";
		switch (batch.m_State)
		{
		case ParameterBatchState::Applying:
			state = "Applying";
			break;
		case ParameterBatchState::Finished:
			state = "Finished";
			break;
		case ParameterBatchState::Failed:
			state = "Failed";
			break;
		default:
			break;
		}
		bool succeeded = batch.m_State == ParameterBatchState::Finished;
		Json items = Json::array();
		for (const auto& item : batch.m_Items)
		{
			succeeded &= item.m_State == ParameterEffectState::Consumed ||
				item.m_State == ParameterEffectState::NoChange;
			items.push_back({
				{ "id", item.m_Parameter.GetName() },
				{ "declaredClass", EffectClass(item.m_DeclaredClass) },
				{ "state", EffectState(item.m_State) },
				{ "identity", { { "revision", item.m_Identity.m_Revision },
					{ "domain", item.m_Identity.m_Domain }, { "generation", item.m_Identity.m_Generation } } },
				{ "requested", item.m_Requested },
				{ "applied", item.m_Applied ? Json(*item.m_Applied) : Json() },
				{ "published", item.m_Published ? Json(*item.m_Published) : Json() },
				{ "firstConsumed", item.m_FirstConsumed ? Json(*item.m_FirstConsumed) : Json() },
				{ "failure", item.m_Failure } });
		}
		Json values = Json::array();
		for (const auto& value : batch.m_Values)
		{
			values.push_back({ { "id", value.m_Id.GetName() }, { "value", Value(value.m_Value) } });
		}
		Json result = {
			{ "batchId", batch.m_BatchId },
			{ "source", batch.m_Source },
			{ "contentId", batch.m_ContentId },
			{ "state", state },
			{ "terminal", batch.IsTerminal() },
			{ "succeeded", batch.IsTerminal() ? Json(succeeded) : Json() },
			{ "revision", batch.m_Revision == 0 ? Json() : Json(batch.m_Revision) },
			{ "impact", Impact(batch.m_Impact) },
			{ "values", std::move(values) },
			{ "items", std::move(items) },
			{ "failure", batch.m_Failure } };
		if (const auto& operation = batch.m_HostViewOperation)
		{
			Json overrides = Json::array();
			for (const auto& entry : operation->m_Overrides)
			{
				auto state = HostOverride(entry.m_Value);
				state["id"] = entry.m_Id.GetName();
				state["type"] = ParameterTypes[static_cast<size_t>(entry.m_Type)];
				overrides.push_back(std::move(state));
			}
			Json changes = Json::array();
			for (const auto& change : operation->m_Changes)
			{
				changes.push_back({
					{ "id", change.m_Id.GetName() },
					{ "before", HostOverride(change.m_Before) },
					{ "after", HostOverride(change.m_After) } });
			}
			const auto stamp = [](const std::optional<HostViewControlStamp>& value)
				{
					return value ? SerializeApplicationControlHostViewStamp(*value) : Json();
				};
			// Unknown until the operation commits; an operation that ended uncommitted changed nothing.
			Json changed;
			if (operation->m_After)
			{
				changed = !operation->m_Changes.empty();
			}
			else if (batch.IsTerminal())
			{
				changed = false;
			}
			result["overrides"] = std::move(overrides);
			// Control evidence, separate from effect items: a pin can change the control
			// state while its effect item is truthfully NoChange.
			result["controlCommit"] = {
				{ "expected", SerializeApplicationControlHostViewStamp(operation->m_Expected) },
				{ "before", stamp(operation->m_Before) },
				{ "after", stamp(operation->m_After) },
				{ "changed", std::move(changed) },
				{ "changes", std::move(changes) },
				{ "conflict", operation->m_Conflict.empty() ? Json() : Json(operation->m_Conflict) } };
		}
		if (const auto& operation = batch.m_DomainOperation)
		{
			const auto stamp = [](const std::optional<ParameterControlStamp>& value)
				{
					return value ? SerializeApplicationControlDomainStamp(*value) : Json();
				};
			result["domainControl"] = {
				{ "expected", SerializeApplicationControlDomainStamp(operation->m_Expected) },
				{ "before", stamp(operation->m_Before) },
				{ "after", stamp(operation->m_After) },
				{ "committed", operation->m_Committed },
				{ "conflict", operation->m_Conflict.empty() ? Json() : Json(operation->m_Conflict) } };
		}
		return result;
	}

	Json SerializeApplicationControlDomainStamp(const ParameterControlStamp& stamp)
	{
		return {
			{ "runIdentity", stamp.m_RunIdentity },
			{ "contentIdentity", stamp.m_ContentIdentity },
			{ "activationRevision", stamp.m_ActivationRevision },
			{ "domain", stamp.m_Domain == ParameterControlDomain::Lab ? "lab" : "lighting-profile" },
			{ "labRevision", stamp.m_LabRevision },
			{ "valuesRevision", stamp.m_ValuesRevision },
			{ "worldRevision", stamp.m_WorldRevision },
			{ "environmentRevision", stamp.m_EnvironmentRevision } };
	}

	Json SerializeApplicationControlLightingProfiles(const LightingProfilePublication* profiles)
	{
		const auto state = [](const std::optional<LightingProfileState>& value) -> Json
			{
				return value ?
					Json{
						{ "profileId", value->m_Id },
						{ "profileHash", Sha256DigestToHex(value->m_Hash) },
						{ "generation", value->m_Generation } } :
					Json();
			};
		return profiles ?
			Json{
				{ "requested", state(profiles->m_Requested) },
				{ "active", state(profiles->m_Active) } } :
			Json();
	}

	Json SerializeApplicationControlHostViewStamp(const HostViewControlStamp& stamp)
	{
		return {
			{ "runIdentity", stamp.m_RunIdentity.empty() ? Json() : Json(stamp.m_RunIdentity) },
			{ "contentIdentity", stamp.m_ContentIdentity },
			{ "hostViewRevision", stamp.m_HostViewRevision } };
	}

	Json SerializeApplicationControlHostViewProjection(std::span<const HostViewProjectionEntry> projection)
	{
		Json result = Json::array();
		for (const auto& entry : projection)
		{
			result.push_back({
				{ "id", entry.m_Id.GetName() },
				{ "type", ParameterTypes[static_cast<size_t>(entry.m_Type)] },
				{ "value", Value(entry.m_Value) },
				{ "source", GetHostViewValueSourceName(entry.m_Source) },
				{ "inheritedSource", GetHostViewValueSourceName(entry.m_InheritedSource) },
				{ "hostOverride", HostOverride(entry.m_Override) } });
		}
		return result;
	}

	std::string SerializeApplicationControlParameters(uint64_t id, GGLabAppRuntime* runtime) noexcept
	{
		const auto* parameterControl = runtime ? runtime->GetParameterControl() : nullptr;
		if (!parameterControl || !parameterControl->IsAvailable())
		{
			return SerializeApplicationControlError(id, "NoParameterSource: ready active content is required.");
		}
		runtime->EnableParameterControl();
		Json response = Response(id, true);
		response["contentId"] = parameterControl->GetContentId();
		response["revision"] = parameterControl->GetParameterRevision();
		response["controlState"] = SerializeApplicationControlHostViewStamp(runtime->GetHostViewControlStamp());
		response["domainControlStates"] = Json::object();
		for (const auto domain : { ParameterControlDomain::Lab, ParameterControlDomain::LightingProfile })
		{
			const auto stamp = runtime->GetDomainControlStamp(domain);
			response["domainControlStates"][domain == ParameterControlDomain::Lab ? "lab" : "lighting-profile"] =
				stamp ? SerializeApplicationControlDomainStamp(*stamp) : Json();
		}
		const auto hostView = parameterControl->DescribeHostView();
		response["parameters"] = Json::array();
		response["lighting"] = SerializeApplicationControlLightingProfiles(&parameterControl->GetLightingProfilePublication());
		response["lightingProfileError"] = parameterControl->GetLightingProfileError();
		response["lightingProfiles"] = Json::array();
		for (const auto& profile : parameterControl->GetLightingProfiles())
		{
			response["lightingProfiles"].push_back({
				{ "id", profile.m_Id },
				{ "hashVersion", LightingProfileHashVersion },
				{ "hash", Sha256DigestToHex(HashLightingProfile(profile)) },
				{ "recommendedEV100", profile.m_RecommendedEV100 ? Json(*profile.m_RecommendedEV100) : Json() } });
		}
		const auto append = [&](const LabParameterSet& source, const char* sourceName)
			{
				for (const auto& parameter : source.GetParameters())
				{
					const auto& desc = parameter.m_Desc;
					Json enums = Json::array();
					for (const auto& item : desc.m_EnumItems)
					{
						enums.push_back({ { "value", item.m_Value }, { "name", item.m_Name } });
					}
					response["parameters"].push_back({
						{ "id", desc.m_Id.GetName() },
						{ "source", sourceName },
						{ "name", desc.m_Name },
						{ "group", desc.m_Group },
						{ "type", ParameterTypes[static_cast<size_t>(desc.m_Type)] },
						{ "impact", Impact(desc.m_Impact) },
						{ "declaredClass", EffectClass(GetParameterEffectClass(desc)) },
						{ "setSupported", parameterControl->SupportsParameterEffect(desc) },
						{ "editPolicy", desc.m_EditPolicy == LabParameterEditPolicy::Continuous ? "Continuous" : "CommitOnEditEnd" },
						{ "default", Value(desc.m_DefaultValue) },
						{ "current", Value(parameter.m_Value) },
						{ "min", desc.m_MinValue ? Value(*desc.m_MinValue) : Json() },
						{ "max", desc.m_MaxValue ? Value(*desc.m_MaxValue) : Json() },
						{ "enumItems", std::move(enums) } });
					const auto entry = std::ranges::find(hostView, desc.m_Id, &HostViewProjectionEntry::m_Id);
					auto& item = response["parameters"].back();
					item["overrideOperationSupported"] = entry != hostView.end();
					if (entry == hostView.end())
					{
						// environment.profile is the only Host parameter outside the display view.
						item["overrideOperationReason"] = std::string_view(sourceName) == "lab" ?
							"lab-parameter" :
							"lighting-profile";
					}
					else
					{
						item["hostOverride"] = HostOverride(entry->m_Override);
						// The next frame's value before sequence or reference patches.
						item["resolved"] = {
							{ "value", Value(entry->m_Value) },
							{ "source", GetHostViewValueSourceName(entry->m_Source) },
							{ "scope", "display-view" } };
						item["inheritedSource"] = GetHostViewValueSourceName(entry->m_InheritedSource);
					}
				}
			};
		append(parameterControl->GetHostParameters(), "host");
		if (const auto* lab = parameterControl->GetLabParameters())
		{
			append(*lab, "lab");
		}
		return Dump(response);
	}

	std::string SubmitApplicationControlParameters(uint64_t id,
		std::span<const ApplicationControlParameterChange> changes, GGLabAppRuntime* runtime,
		const ParameterControlStamp* expected) noexcept
	{
		const auto reject = [&](std::string_view error)
			{
				if (!expected)
				{
					return SerializeApplicationControlError(id, error);
				}
				Json response = Response(id, false);
				response["error"] = error;
				response["errorCode"] = error.find(':') == std::string_view::npos ?
					"ParameterDomainInvalid" :
					std::string(error.substr(0, error.find(':')));
				const auto stamp = runtime ? runtime->GetDomainControlStamp(expected->m_Domain) : std::nullopt;
				response["domainControlState"] = stamp ? SerializeApplicationControlDomainStamp(*stamp) : Json();
				return Dump(response);
			};
		if (expected && runtime)
		{
			ApplicationControlRequest check{ .m_Id = id };
			check.m_ExpectedRunIdentity = expected->m_RunIdentity;
			if (const auto rejection = CheckApplicationControlRunIdentity(check,
				runtime->GetHostViewControlStamp().m_RunIdentity))
			{
				return *rejection;
			}
		}
		std::string error = "NoParameterSource: ready active content is required.";
		if (!runtime || !runtime->CanRequestParameters(error))
		{
			return reject(error);
		}
		runtime->EnableParameterControl();
		const auto* parameterControl = runtime->GetParameterControl();
		ParameterRequest request;
		if (expected)
		{
			request.m_ExpectedDomain = *expected;
		}
		for (const auto& change : changes)
		{
			const auto* parameter = parameterControl->GetHostParameters().Find(change.m_Value.m_Id);
			if (!parameter && parameterControl->GetLabParameters())
			{
				parameter = parameterControl->GetLabParameters()->Find(change.m_Value.m_Id);
			}
			if (!parameter || parameter->m_Desc.m_Type != change.m_Type)
			{
				return reject("Unknown parameter or a type different from its descriptor.");
			}
			request.m_Entries.push_back({ change.m_Value.m_Id, change.m_Type,
				ParameterOperation::Assign, change.m_Value.m_Value });
		}
		const uint64_t batchId = runtime->RequestParameters(request, error);
		if (batchId == 0)
		{
			return reject(error);
		}
		Json response = Response(id, true);
		response["admission"] = "Accepted";
		response["batchId"] = batchId;
		response["consumed"] = false;
		response["batch"] = SerializeApplicationControlParameterBatch(*parameterControl->GetParameterBatchStatus());
		return Dump(response);
	}

	std::string SubmitApplicationControlHostViewOverrides(uint64_t id, const HostViewControlStamp& expected,
		std::span<const HostViewOverrideState> overrides, GGLabAppRuntime* runtime) noexcept
	{
		std::string error = "NoParameterSource: ready active content is required.";
		const uint64_t batchId = runtime ?
			runtime->RequestParameters(ParameterRequest::HostViewOverrides(expected, overrides), error) :
			0;
		if (batchId == 0)
		{
			Json response = Response(id, false);
			response["errorCode"] = ErrorCode(error);
			response["error"] = error;
			response["controlState"] = runtime ?
				SerializeApplicationControlHostViewStamp(runtime->GetHostViewControlStamp()) :
				Json();
			return Dump(response);
		}
		Json response = Response(id, true);
		response["admission"] = "Accepted";
		response["batchId"] = batchId;
		response["consumed"] = false;
		response["batch"] = SerializeApplicationControlParameterBatch(*runtime->GetExternalParameterBatchStatus());
		return Dump(response);
	}

	std::string QueryApplicationControlParameters(
		uint64_t id, uint64_t batchId, bool consume, GGLabAppRuntime* runtime) noexcept
	{
		const auto* batch = runtime ? runtime->GetExternalParameterBatchStatus() : nullptr;
		if (!batch || batch->m_BatchId != batchId)
		{
			Json response = Response(id, false);
			response["batchId"] = batchId;
			response["status"] = "not-found";
			response["error"] = "Unknown or consumed parameter batch id in this session.";
			return Dump(response);
		}
		Json response = Response(id, true);
		response["batchId"] = batchId;
		response["batch"] = SerializeApplicationControlParameterBatch(*batch);
		const bool consumed = consume && batch->IsTerminal();
		response["consumed"] = consumed;
		const std::string serialized = Dump(response);
		if (consumed)
		{
			GGLAB_UNUSED(runtime->TakeParameterBatchResult(batchId));
		}
		return serialized;
	}
}
