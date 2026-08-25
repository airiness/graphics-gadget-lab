#pragma once
#include "Contracts/ShaderCompileTarget.h"

#include <array>
#include <string>
#include <string_view>
#include <vector>

namespace gglab
{
	// Single table authority for the target-profile <-> wire-name mapping.
	// Every consumer of the target set derives from this one table: the CLI
	// accept path (ParseShaderTarget -> Parse), the describe supportedTargets
	// report, and the --targets enumeration (-> Names). Because Parse and
	// Names are both derived from kEntries, the accepted set and the reported
	// set cannot drift apart.
	namespace ShaderTargetWire
	{
		struct Entry
		{
			std::string_view name;
			ShaderTargetProfile profile;
		};

		// The one and only table. Order is the stable report order and the
		// only source of the wire name strings.
		inline constexpr std::array<Entry, 2> kEntries{
			Entry{ "gglab-dx12", ShaderTargetProfile::GGLabDX12 },
			Entry{ "gglab-vulkan13", ShaderTargetProfile::GGLabVulkan13 },
		};

		// Accept side: parse a wire name into its target profile.
		[[nodiscard]] constexpr bool Parse(
			std::string_view name, ShaderTargetProfile& outProfile) noexcept
		{
			for (const Entry& entry : kEntries)
			{
				if (name == entry.name)
				{
					outProfile = entry.profile;
					return true;
				}
			}
			return false;
		}

		// Report side: the stable set of supported wire names, in table order.
		[[nodiscard]] std::vector<std::string> Names() noexcept
		{
			std::vector<std::string> names;
			names.reserve(kEntries.size());
			for (const Entry& entry : kEntries)
			{
				names.push_back(std::string(entry.name));
			}
			return names;
		}
	}
}
