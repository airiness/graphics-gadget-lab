#include "Application/Control/ApplicationControlParameters.h"
#include "GGLabFoundation/Hash/Sha256.h"
#include "GGLabRuntime/Graphics/LightingProfile.h"
#include "Application/Control/ApplicationControlProtocol.h"
#include "Parameters/HostViewParameters.h"
#include "Parameters/SessionParameterControl.h"
#include "GGLabAppRuntime.h"
#include "GGLabFoundation/Base/CoreMacros.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
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

		[[nodiscard]] Json HostOverride(const HostViewProjectionEntry& entry)
		{
			return entry.m_Override ?
				Json{ { "present", true }, { "value", Value(*entry.m_Override) } } :
				Json{ { "present", false } };
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
		return {
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
				{ "hostOverride", HostOverride(entry) } });
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
					if (entry != hostView.end())
					{
						auto& item = response["parameters"].back();
						item["hostOverride"] = HostOverride(*entry);
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
		std::span<const ApplicationControlParameterChange> changes, GGLabAppRuntime* runtime) noexcept
	{
		std::string error = "NoParameterSource: ready active content is required.";
		if (!runtime || !runtime->CanRequestParameterBatch(error))
		{
			return SerializeApplicationControlError(id, error);
		}
		runtime->EnableParameterControl();
		const auto* parameterControl = runtime->GetParameterControl();
		std::vector<LabParameterValue> values;
		for (const auto& change : changes)
		{
			const auto* parameter = parameterControl->GetHostParameters().Find(change.m_Value.m_Id);
			if (!parameter && parameterControl->GetLabParameters())
			{
				parameter = parameterControl->GetLabParameters()->Find(change.m_Value.m_Id);
			}
			if (!parameter || parameter->m_Desc.m_Type != change.m_Type)
			{
				return SerializeApplicationControlError(id, "Unknown parameter or a type different from its descriptor.");
			}
			values.push_back(change.m_Value);
		}
		const uint64_t batchId = runtime->RequestParameterBatch(values, error);
		if (batchId == 0)
		{
			return SerializeApplicationControlError(id, error);
		}
		Json response = Response(id, true);
		response["admission"] = "Accepted";
		response["batchId"] = batchId;
		response["consumed"] = false;
		response["batch"] = SerializeApplicationControlParameterBatch(*parameterControl->GetParameterBatchStatus());
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
