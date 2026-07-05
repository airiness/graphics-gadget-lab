#pragma once
#include "Core/StringId.h"

#include <string>
#include <string_view>

namespace gglab
{
	struct LabId
	{
		LabId() noexcept = default;
		explicit LabId(std::string_view name) noexcept :
			m_Hash(name),
			m_Name(name)
		{}

		bool IsValid() const noexcept { return !m_Name.empty(); }
		std::string_view GetName() const noexcept { return m_Name; }
		StringID GetHash() const noexcept { return m_Hash; }

		bool operator==(const LabId& other) const noexcept
		{
			return m_Hash == other.m_Hash && m_Name == other.m_Name;
		}

		StringID m_Hash{};
		std::string m_Name;
	};

	enum class LabKind : uint8_t
	{
		Scene,
		Pipeline,
	};

	enum class LabRunState : uint8_t
	{
		Uninitialized,
		Loading,
		WarmingUp,
		Ready,
		Capturing,
		Completed,
		Failed,
	};

	struct LabDescriptor
	{
		LabId m_Id;
		std::string m_DisplayName;
		std::string m_Category;
		std::string m_Description;
		LabKind m_Kind = LabKind::Scene;
		uint32_t m_SchemaVersion = 1;
	};
}
