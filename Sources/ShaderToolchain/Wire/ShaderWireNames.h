#pragma once
#include "Contracts/ShaderCompileTarget.h"

#include <array>
#include <string_view>

namespace gglab
{
	namespace ShaderBinaryFormatWire
	{
		struct Entry final { std::string_view m_Name; ShaderBinaryFormat m_Value; };
		inline constexpr std::array Entries{
			Entry{ "dxil", ShaderBinaryFormat::Dxil },
			Entry{ "spirv", ShaderBinaryFormat::SpirV },
		};

		[[nodiscard]] constexpr std::string_view Name(ShaderBinaryFormat value) noexcept
		{
			for (const Entry& entry : Entries)
			{
				if (entry.m_Value == value) { return entry.m_Name; }
			}
			return "unknown";
		}

		[[nodiscard]] constexpr bool Parse(
			std::string_view name, ShaderBinaryFormat& outValue) noexcept
		{
			for (const Entry& entry : Entries)
			{
				if (entry.m_Name == name)
				{
					outValue = entry.m_Value;
					return true;
				}
			}
			return false;
		}
	}

	namespace ShaderSpirVTargetEnvironmentWire
	{
		struct Entry final
		{
			std::string_view m_Name;
			ShaderSpirVTargetEnvironment m_Value;
		};
		inline constexpr std::array Entries{
			Entry{ "none", ShaderSpirVTargetEnvironment::None },
			Entry{ "vulkan1.3", ShaderSpirVTargetEnvironment::Vulkan1_3 },
		};

		[[nodiscard]] constexpr std::string_view Name(
			ShaderSpirVTargetEnvironment value) noexcept
		{
			for (const Entry& entry : Entries)
			{
				if (entry.m_Value == value) { return entry.m_Name; }
			}
			return "unknown";
		}

		[[nodiscard]] constexpr bool Parse(
			std::string_view name, ShaderSpirVTargetEnvironment& outValue) noexcept
		{
			for (const Entry& entry : Entries)
			{
				if (entry.m_Name == name)
				{
					outValue = entry.m_Value;
					return true;
				}
			}
			return false;
		}
	}

	namespace ShaderStageWire
	{
		struct Entry final { std::string_view m_Name; ShaderStage m_Value; };
		inline constexpr std::array Entries{
			Entry{ "vertex", ShaderStage::Vertex },
			Entry{ "pixel", ShaderStage::Pixel },
			Entry{ "hull", ShaderStage::Hull },
			Entry{ "domain", ShaderStage::Domain },
			Entry{ "geometry", ShaderStage::Geometry },
			Entry{ "mesh", ShaderStage::Mesh },
			Entry{ "compute", ShaderStage::Compute },
		};

		[[nodiscard]] constexpr std::string_view Name(ShaderStage value) noexcept
		{
			for (const Entry& entry : Entries)
			{
				if (entry.m_Value == value) { return entry.m_Name; }
			}
			return "unknown";
		}

		[[nodiscard]] constexpr bool Parse(
			std::string_view name, ShaderStage& outValue) noexcept
		{
			for (const Entry& entry : Entries)
			{
				if (entry.m_Name == name)
				{
					outValue = entry.m_Value;
					return true;
				}
			}
			return false;
		}
	}

	namespace ShaderModelWire
	{
		struct Entry final { std::string_view m_Name; ShaderModel m_Value; };
		inline constexpr std::array Entries{
			Entry{ "6_6", ShaderModel::SM_6_6 },
			Entry{ "6_7", ShaderModel::SM_6_7 },
			Entry{ "6_8", ShaderModel::SM_6_8 },
		};

		[[nodiscard]] constexpr std::string_view Name(ShaderModel value) noexcept
		{
			for (const Entry& entry : Entries)
			{
				if (entry.m_Value == value) { return entry.m_Name; }
			}
			return "unknown";
		}

		[[nodiscard]] constexpr bool Parse(
			std::string_view name, ShaderModel& outValue) noexcept
		{
			for (const Entry& entry : Entries)
			{
				if (entry.m_Name == name)
				{
					outValue = entry.m_Value;
					return true;
				}
			}
			return false;
		}
	}
}
