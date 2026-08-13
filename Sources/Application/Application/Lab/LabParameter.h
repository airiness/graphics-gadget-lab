#pragma once
#include "Core/StringId.h"
#include "Core/Math/Color.h"
#include "Core/Math/Vector.h"

#include <optional>
#include <span>
#include <string>
#include <variant>
#include <vector>

namespace gglab
{
	struct LabParameterId
	{
		LabParameterId() noexcept = default;
		explicit LabParameterId(std::string_view name) noexcept :
			m_Hash(name),
			m_Name(name)
		{}

		bool IsValid() const noexcept { return !m_Name.empty(); }
		std::string_view GetName() const noexcept { return m_Name; }

		bool operator==(const LabParameterId& other) const noexcept
		{
			return m_Hash == other.m_Hash && m_Name == other.m_Name;
		}

		StringID m_Hash{};
		std::string m_Name;
	};

	using LabValue = std::variant<
		bool,
		int32_t,
		uint32_t,
		float,
		Vector3,
		Color>;

	enum class LabParameterType : uint8_t
	{
		Bool,
		Int,
		UInt,
		Float,
		Enum,
		Vector3,
		Color,
	};

	enum class LabChangeImpact : uint8_t
	{
		Immediate,
		RebuildScene,
		RecreatePipeline,
		RestartSession,
	};

	enum class LabParameterEditPolicy : uint8_t
	{
		Continuous,
		CommitOnEditEnd,
	};

	struct LabEnumItem
	{
		int32_t m_Value = 0;
		std::string m_Name;
	};

	struct LabParameterDesc
	{
		LabParameterId m_Id;
		std::string m_Name;
		std::string m_Group;
		LabParameterType m_Type = LabParameterType::Bool;
		LabChangeImpact m_Impact = LabChangeImpact::Immediate;
		LabParameterEditPolicy m_EditPolicy = LabParameterEditPolicy::Continuous;
		LabValue m_DefaultValue = false;
		std::optional<LabValue> m_MinValue;
		std::optional<LabValue> m_MaxValue;
		std::vector<LabEnumItem> m_EnumItems;
	};

	struct LabParameter
	{
		LabParameterDesc m_Desc;
		LabValue m_Value = false;
		bool m_Dirty = false;
	};

	struct LabParameterValue
	{
		LabParameterId m_Id;
		LabValue m_Value = false;
	};

	class LabParameterSet
	{
	public:
		bool Add(LabParameterDesc desc) noexcept;
		LabChangeImpact ResetAll() noexcept;
		bool Set(const LabParameterId& id, const LabValue& value,
			LabChangeImpact* impact = nullptr) noexcept;

		const LabParameter* Find(const LabParameterId& id) const noexcept;
		std::span<const LabParameter> GetParameters() const noexcept { return m_Parameters; }
		std::vector<LabParameterValue> CaptureValues() const noexcept;

		template<typename T>
		T Get(const LabParameterId& id, const T& fallback) const noexcept
		{
			const LabParameter* parameter = Find(id);
			if (parameter)
			{
				if (const auto* value = std::get_if<T>(&parameter->m_Value))
				{
					return *value;
				}
			}
			return fallback;
		}

	private:
		static bool IsValueCompatible(LabParameterType type, const LabValue& value) noexcept;
		static LabValue SanitizeValue(const LabParameterDesc& desc, const LabValue& value) noexcept;
		static LabChangeImpact MaxImpact(LabChangeImpact lhs, LabChangeImpact rhs) noexcept;

		std::vector<LabParameter> m_Parameters;
	};
}
