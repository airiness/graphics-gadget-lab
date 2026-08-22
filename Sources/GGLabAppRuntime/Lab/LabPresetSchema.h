#pragma once
#include "Lab/LabParameter.h"
#include "Lab/LabRunConfig.h"
#include "Lab/LabTypes.h"

namespace gglab
{
	struct LabPresetId
	{
		LabPresetId() noexcept = default;
		explicit LabPresetId(std::string_view name) noexcept :
			m_Hash(name),
			m_Name(name)
		{}

		bool IsValid() const noexcept { return !m_Name.empty(); }

		StringID m_Hash{};
		std::string m_Name;
	};

	struct LabPresetSchema
	{
		static constexpr uint32_t CurrentFormatVersion = 1;

		uint32_t m_FormatVersion = CurrentFormatVersion;
		LabPresetId m_PresetId;
		LabId m_LabId;
		uint32_t m_LabSchemaVersion = 0;
		std::string m_DisplayName;
		LabRunConfig m_RunConfig{};
		std::vector<LabParameterValue> m_ParameterValues;

		bool IsCompatible(const LabDescriptor& descriptor) const noexcept
		{
			return m_FormatVersion == CurrentFormatVersion && m_PresetId.IsValid() &&
				m_LabId == descriptor.m_Id && m_LabSchemaVersion == descriptor.m_SchemaVersion;
		}
	};
}
