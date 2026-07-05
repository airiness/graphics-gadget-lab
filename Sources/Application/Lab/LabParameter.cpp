#include "Core/Precompiled.h"
#include "Application/Lab/LabParameter.h"

namespace gglab
{
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
		const LabParameterId& id,
		const LabValue& value,
		LabChangeImpact* impact) noexcept
	{
		const auto iter = std::ranges::find_if(m_Parameters, [&id](const LabParameter& parameter)
			{
				return parameter.m_Desc.m_Id == id;
			});
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

	const LabParameter* LabParameterSet::Find(const LabParameterId& id) const noexcept
	{
		const auto iter = std::ranges::find_if(m_Parameters, [&id](const LabParameter& parameter)
			{
				return parameter.m_Desc.m_Id == id;
			});
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

	bool LabParameterSet::IsValueCompatible(
		LabParameterType type,
		const LabValue& value) noexcept
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
		const LabParameterDesc& desc,
		const LabValue& value) noexcept
	{
		if (desc.m_Type == LabParameterType::Enum)
		{
			const int32_t candidate = std::get<int32_t>(value);
			const bool exists = std::ranges::any_of(desc.m_EnumItems, [candidate](const LabEnumItem& item)
				{
					return item.m_Value == candidate;
				});
			return exists ? value : desc.m_DefaultValue;
		}

		return std::visit([&desc]<typename T>(const T& candidate) -> LabValue
			{
				if constexpr (std::is_same_v<T, int32_t> ||
					std::is_same_v<T, uint32_t> || std::is_same_v<T, float>)
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
							result.x = std::max(result.x, minValue->x);
							result.y = std::max(result.y, minValue->y);
							result.z = std::max(result.z, minValue->z);
						}
					}
					if (desc.m_MaxValue)
					{
						if (const auto* maxValue = std::get_if<Vector3>(&*desc.m_MaxValue))
						{
							result.x = std::min(result.x, maxValue->x);
							result.y = std::min(result.y, maxValue->y);
							result.z = std::min(result.z, maxValue->z);
						}
					}
					return result;
				}
				else if constexpr (std::is_same_v<T, Color>)
				{
					return Color(
						std::clamp(candidate.x, 0.0f, 1.0f),
						std::clamp(candidate.y, 0.0f, 1.0f),
						std::clamp(candidate.z, 0.0f, 1.0f),
						std::clamp(candidate.w, 0.0f, 1.0f));
				}
				else
				{
					return candidate;
				}
			}, value);
	}

	LabChangeImpact LabParameterSet::MaxImpact(
		LabChangeImpact lhs,
		LabChangeImpact rhs) noexcept
	{
		return static_cast<LabChangeImpact>(std::max(
			static_cast<uint8_t>(lhs),
			static_cast<uint8_t>(rhs)));
	}
}
