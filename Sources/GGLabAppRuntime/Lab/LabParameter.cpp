#include "Lab/LabParameter.h"
#include "GGLabFoundation/Base/CoreMacros.h"
#include "AppRuntimeLog.h"
#include "GGLabFoundation/String/StringUtils.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <format>
#include <system_error>
#include <type_traits>

namespace gglab
{
	namespace
	{
		template <typename T>
		[[nodiscard]] std::optional<T> ParseNumber(std::string_view text) noexcept
		{
			T result{};
			const char* end = text.data() + text.size();
			const auto [last, error] = std::from_chars(text.data(), end, result);
			if (text.empty() || error != std::errc{} || last != end)
			{
				return std::nullopt;
			}
			return result;
		}

		template <typename T>
		[[nodiscard]] std::optional<LabValue> ParseRangedNumber(
			const LabParameterDesc& desc, std::string_view text, std::string& error) noexcept
		{
			const std::optional<T> value = ParseNumber<T>(text);
			if (!value)
			{
				error = std::format("'{}' is not a valid number for this parameter", text);
				return std::nullopt;
			}
			if constexpr (std::is_same_v<T, float>)
			{
				if (!std::isfinite(*value))
				{
					error = std::format("'{}' is not finite", text);
					return std::nullopt;
				}
			}
			const T* minimum = desc.m_MinValue ? std::get_if<T>(&*desc.m_MinValue) : nullptr;
			const T* maximum = desc.m_MaxValue ? std::get_if<T>(&*desc.m_MaxValue) : nullptr;
			if ((minimum && *value < *minimum) || (maximum && *value > *maximum))
			{
				error = std::format("'{}' is outside the declared range", text);
				return std::nullopt;
			}
			return LabValue(*value);
		}
	}

	ParameterEffectClass GetParameterEffectClass(const LabParameterDesc& desc) noexcept
	{
		return desc.m_Impact == LabChangeImpact::Immediate ?
			desc.m_EffectClass :
			ParameterEffectClass::Rebuild;
	}

	bool LabValuesEqual(const LabValue& lhs, const LabValue& rhs) noexcept
	{
		if (lhs.index() != rhs.index())
		{
			return false;
		}
		return std::visit([&rhs]<typename T>(const T& left)
			{
				const T& right = std::get<T>(rhs);
				if constexpr (std::is_same_v<T, Vector3>)
				{
					return left.m_X == right.m_X && left.m_Y == right.m_Y && left.m_Z == right.m_Z;
				}
				else if constexpr (std::is_same_v<T, Color>)
				{
					return left.m_R == right.m_R && left.m_G == right.m_G &&
						left.m_B == right.m_B && left.m_A == right.m_A;
				}
				else
				{
					return left == right;
				}
			}, lhs);
	}

	bool LabParameterSet::Add(LabParameterDesc desc) noexcept
	{
		if (!desc.m_Id.IsValid() || desc.m_Name.empty() || Find(desc.m_Id) ||
			!IsValueCompatible(desc.m_Type, desc.m_DefaultValue) ||
			(desc.m_MinValue && !IsValueCompatible(desc.m_Type, *desc.m_MinValue)) ||
			(desc.m_MaxValue && !IsValueCompatible(desc.m_Type, *desc.m_MaxValue)))
		{
			GGLAB_LOG_ERROR("Cannot register invalid lab parameter '{}'.", desc.m_Id.GetName());
			return false;
		}

		if (desc.m_Type == LabParameterType::Enum && desc.m_EnumItems.empty())
		{
			GGLAB_LOG_ERROR("Enum lab parameter '{}' has no items.", desc.m_Id.GetName());
			return false;
		}

		desc.m_DefaultValue = SanitizeValue(desc, desc.m_DefaultValue);
		m_Parameters.push_back({
			.m_Desc = std::move(desc),
			.m_Value = false,
			.m_Dirty = false,
			});
		m_Parameters.back().m_Value = m_Parameters.back().m_Desc.m_DefaultValue;
		return true;
	}

	LabChangeImpact LabParameterSet::ResetAll() noexcept
	{
		LabChangeImpact impact = LabChangeImpact::Immediate;
		for (LabParameter& parameter : m_Parameters)
		{
			parameter.m_Value = parameter.m_Desc.m_DefaultValue;
			parameter.m_Dirty = true;
			impact = MaxImpact(impact, parameter.m_Desc.m_Impact);
		}
		return impact;
	}

	bool LabParameterSet::Set(
		const LabParameterId& id, const LabValue& value, LabChangeImpact* impact) noexcept
	{
		const auto iter = std::ranges::find_if(m_Parameters,
			[&id](const LabParameter& parameter) { return parameter.m_Desc.m_Id == id; });
		if (iter == m_Parameters.end() || !IsValueCompatible(iter->m_Desc.m_Type, value))
		{
			return false;
		}

		iter->m_Value = SanitizeValue(iter->m_Desc, value);
		iter->m_Dirty = true;
		if (impact)
		{
			*impact = iter->m_Desc.m_Impact;
		}
		return true;
	}

	bool LabParameterSet::ValidateValue(const LabParameterDesc& desc,
		const LabValue& value, std::string& error) noexcept
	{
		error.clear();
		if (!IsValueCompatible(desc.m_Type, value))
		{
			error = "the value has the wrong type";
			return false;
		}
		if (desc.m_Type == LabParameterType::Enum &&
			!std::ranges::any_of(desc.m_EnumItems, [&](const LabEnumItem& item)
				{ return item.m_Value == std::get<int32_t>(value); }))
		{
			error = "the value is not a declared enum item";
			return false;
		}
		const bool valid = std::visit([&desc]<typename T>(const T& candidate)
			{
				const T* minimum = desc.m_MinValue ? std::get_if<T>(&*desc.m_MinValue) : nullptr;
				const T* maximum = desc.m_MaxValue ? std::get_if<T>(&*desc.m_MaxValue) : nullptr;
				if constexpr (std::is_same_v<T, float> || std::is_same_v<T, int32_t> ||
					std::is_same_v<T, uint32_t>)
				{
					return std::isfinite(candidate) && (!minimum || candidate >= *minimum) &&
						(!maximum || candidate <= *maximum);
				}
				else if constexpr (std::is_same_v<T, Vector3> || std::is_same_v<T, Color>)
				{
					constexpr size_t components = std::is_same_v<T, Vector3> ? 3 : 4;
					for (size_t component = 0; component < components; ++component)
					{
						const float scalar = candidate[component];
						if (!std::isfinite(scalar) || (minimum && scalar < (*minimum)[component]) ||
							(maximum && scalar > (*maximum)[component]))
						{
							return false;
						}
						if constexpr (std::is_same_v<T, Color>)
						{
							if (scalar < 0.0f || scalar > 1.0f)
							{
								return false;
							}
						}
					}
					return true;
				}
				else
				{
					return true;
				}
			}, value);
		if (!valid)
		{
			error = "the value must be finite and within the declared range";
		}
		return valid;
	}

	std::optional<LabParameterBatchResult> LabParameterSet::ApplyBatch(
		std::span<const LabParameterValue> changes, std::string& error) noexcept
	{
		error.clear();
		if (changes.empty())
		{
			error = "a parameter batch must not be empty";
			return std::nullopt;
		}
		LabParameterBatchResult result;
		std::vector<size_t> indices;
		indices.reserve(changes.size());
		result.m_Changed.reserve(changes.size());
		for (const LabParameterValue& change : changes)
		{
			const auto found = std::ranges::find_if(m_Parameters,
				[&](const LabParameter& parameter) { return parameter.m_Desc.m_Id == change.m_Id; });
			if (found == m_Parameters.end())
			{
				error = std::format("Unknown parameter '{}'.", change.m_Id.GetName());
				return std::nullopt;
			}
			const size_t index = static_cast<size_t>(found - m_Parameters.begin());
			if (std::ranges::find(indices, index) != indices.end())
			{
				error = std::format("Duplicate parameter '{}'.", change.m_Id.GetName());
				return std::nullopt;
			}
			if (!ValidateValue(found->m_Desc, change.m_Value, error))
			{
				error = std::format("Parameter '{}': {}.", change.m_Id.GetName(), error);
				return std::nullopt;
			}
			indices.push_back(index);
			if (!LabValuesEqual(found->m_Value, change.m_Value))
			{
				result.m_Changed.push_back(change.m_Id);
				result.m_Impact = MaxImpact(result.m_Impact, found->m_Desc.m_Impact);
			}
		}
		// Validation completes before any live value or dirty bit is changed.
		for (size_t item = 0; item < changes.size(); ++item)
		{
			LabParameter& parameter = m_Parameters[indices[item]];
			if (!LabValuesEqual(parameter.m_Value, changes[item].m_Value))
			{
				parameter.m_Value = changes[item].m_Value;
				parameter.m_Dirty = true;
			}
		}
		return result;
	}

	const LabParameter* LabParameterSet::Find(const LabParameterId& id) const noexcept
	{
		const auto iter = std::ranges::find_if(m_Parameters,
			[&id](const LabParameter& parameter) { return parameter.m_Desc.m_Id == id; });
		return iter != m_Parameters.end() ? &*iter : nullptr;
	}

	std::vector<LabParameterValue> LabParameterSet::CaptureValues() const noexcept
	{
		std::vector<LabParameterValue> values;
		values.reserve(m_Parameters.size());
		for (const LabParameter& parameter : m_Parameters)
		{
			values.push_back({
				.m_Id = parameter.m_Desc.m_Id,
				.m_Value = parameter.m_Value,
				});
		}
		return values;
	}

	std::vector<LabParameterValue> LabParameterSet::CaptureRestoredValues() const noexcept
	{
		std::vector<LabParameterValue> values;
		values.reserve(m_Parameters.size());
		for (const auto& parameter : m_Parameters)
		{
			if (!parameter.m_Desc.m_InheritOnEnter || parameter.m_Dirty)
			{
				values.push_back({ parameter.m_Desc.m_Id, parameter.m_Value });
			}
		}
		return values;
	}

	bool LabParameterSet::BindInheritedDefault(const LabParameterId& id, const LabValue& value) noexcept
	{
		const auto found = std::ranges::find_if(m_Parameters,
			[&](const auto& parameter) { return parameter.m_Desc.m_Id == id; });
		std::string error;
		if (found == m_Parameters.end() || !found->m_Desc.m_InheritOnEnter ||
			!ValidateValue(found->m_Desc, value, error))
		{
			return false;
		}
		found->m_Desc.m_DefaultValue = value;
		if (!found->m_Dirty)
		{
			found->m_Value = value;
		}
		return true;
	}

	bool LabParameterSet::FollowInheritedValue(const LabParameterId& id, const LabValue& value) noexcept
	{
		const auto found = std::ranges::find_if(m_Parameters,
			[&](const auto& parameter) { return parameter.m_Desc.m_Id == id; });
		std::string error;
		const bool valid = found != m_Parameters.end() && found->m_Desc.m_InheritOnEnter &&
			ValidateValue(found->m_Desc, value, error);
		GGLAB_ASSERT_MSG(valid, "Only an entered owning domain can resolve a valid inherited parameter.");
		if (!valid)
		{
			return false;
		}
		const bool changed = found->m_Dirty || !LabValuesEqual(found->m_Value, value);
		found->m_Value = value;
		found->m_Dirty = false;
		return changed;
	}

	bool LabParameterSet::IsValueCompatible(LabParameterType type, const LabValue& value) noexcept
	{
		switch (type)
		{
		case LabParameterType::Bool:
			return std::holds_alternative<bool>(value);
		case LabParameterType::Int:
		case LabParameterType::Enum:
			return std::holds_alternative<int32_t>(value);
		case LabParameterType::UInt:
			return std::holds_alternative<uint32_t>(value);
		case LabParameterType::Float:
			return std::holds_alternative<float>(value);
		case LabParameterType::Vector3:
			return std::holds_alternative<Vector3>(value);
		case LabParameterType::Color:
			return std::holds_alternative<Color>(value);
		}
		return false;
	}

	LabValue LabParameterSet::SanitizeValue(
		const LabParameterDesc& desc, const LabValue& value) noexcept
	{
		if (desc.m_Type == LabParameterType::Enum)
		{
			const int32_t candidate = std::get<int32_t>(value);
			const bool exists = std::ranges::any_of(desc.m_EnumItems,
				[candidate](const LabEnumItem& item) { return item.m_Value == candidate; });
			return exists ? value : desc.m_DefaultValue;
		}

		return std::visit(
			[&desc]<typename T>(const T & candidate) -> LabValue
		{
			if constexpr (std::is_same_v<T, int32_t> || std::is_same_v<T, uint32_t> ||
				std::is_same_v<T, float>)
			{
				T result = candidate;
				if (desc.m_MinValue)
				{
					if (const auto* minValue = std::get_if<T>(&*desc.m_MinValue))
					{
						result = std::max(result, *minValue);
					}
				}
				if (desc.m_MaxValue)
				{
					if (const auto* maxValue = std::get_if<T>(&*desc.m_MaxValue))
					{
						result = std::min(result, *maxValue);
					}
				}
				return result;
			}
			else if constexpr (std::is_same_v<T, Vector3>)
			{
				Vector3 result = candidate;
				if (desc.m_MinValue)
				{
					if (const auto* minValue = std::get_if<Vector3>(&*desc.m_MinValue))
					{
						result.m_X = std::max(result.m_X, minValue->m_X);
						result.m_Y = std::max(result.m_Y, minValue->m_Y);
						result.m_Z = std::max(result.m_Z, minValue->m_Z);
					}
				}
				if (desc.m_MaxValue)
				{
					if (const auto* maxValue = std::get_if<Vector3>(&*desc.m_MaxValue))
					{
						result.m_X = std::min(result.m_X, maxValue->m_X);
						result.m_Y = std::min(result.m_Y, maxValue->m_Y);
						result.m_Z = std::min(result.m_Z, maxValue->m_Z);
					}
				}
				return result;
			}
			else if constexpr (std::is_same_v<T, Color>)
			{
				return Color(std::clamp(candidate.m_R, 0.0f, 1.0f),
					std::clamp(candidate.m_G, 0.0f, 1.0f),
					std::clamp(candidate.m_B, 0.0f, 1.0f),
					std::clamp(candidate.m_A, 0.0f, 1.0f));
			}
			else
			{
				return candidate;
			}
		},
			value);
	}

	LabChangeImpact LabParameterSet::MaxImpact(LabChangeImpact lhs, LabChangeImpact rhs) noexcept
	{
		return static_cast<LabChangeImpact>(
			std::max(static_cast<uint8_t>(lhs), static_cast<uint8_t>(rhs)));
	}

	std::optional<LabValue> ParseLabParameterValue(
		const LabParameterDesc& desc, std::string_view text, std::string& error) noexcept
	{
		switch (desc.m_Type)
		{
		case LabParameterType::Bool:
			if (utils::EqualsAsciiIgnoreCase(text, "true") || text == "1")
			{
				return LabValue(true);
			}
			if (utils::EqualsAsciiIgnoreCase(text, "false") || text == "0")
			{
				return LabValue(false);
			}
			error = std::format("'{}' is not true or false", text);
			return std::nullopt;
		case LabParameterType::Int:
			return ParseRangedNumber<int32_t>(desc, text, error);
		case LabParameterType::UInt:
			return ParseRangedNumber<uint32_t>(desc, text, error);
		case LabParameterType::Float:
			return ParseRangedNumber<float>(desc, text, error);
		case LabParameterType::Enum:
		{
			const std::optional<int32_t> number = ParseNumber<int32_t>(text);
			for (const LabEnumItem& item : desc.m_EnumItems)
			{
				if (utils::EqualsAsciiIgnoreCase(text, item.m_Name) ||
					(number && *number == item.m_Value))
				{
					return LabValue(item.m_Value);
				}
			}
			error = std::format("'{}' is not an item of this enum", text);
			return std::nullopt;
		}
		case LabParameterType::Vector3:
		case LabParameterType::Color:
			break;
		}
		error = "the parameter type has no textual form";
		return std::nullopt;
	}
}
