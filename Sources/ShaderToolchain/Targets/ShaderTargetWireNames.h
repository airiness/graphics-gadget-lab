#pragma once
#include "Contracts/ShaderCompileTarget.h"

#include <string>
#include <string_view>
#include <vector>

namespace gglab
{
	// Single source of truth for the target-profile <-> wire-name mapping.
	// The CLI accept path (ParseShaderTarget), the describe report path
	// (ShaderTargetWireNames), and the machine self-description document all
	// derive from this table so the wire contract facts can never drift
	// apart: describe.supportedTargets is asserted value-identical to the
	// ParseTarget acceptance set.
	namespace ShaderTargetWire
	{
		[[nodiscard]] constexpr std::string_view Dx12Name() noexcept
		{
			return "gglab-dx12";
		}

		[[nodiscard]] constexpr std::string_view Vulkan13Name() noexcept
		{
			return "gglab-vulkan13";
		}

		// Accept side: parse a wire name into the target profile.
		[[nodiscard]] constexpr bool Parse(
			std::string_view name, ShaderTargetProfile& outProfile) noexcept
		{
			if (name == Dx12Name())
			{
				outProfile = ShaderTargetProfile::GGLabDX12;
				return true;
			}
			if (name == Vulkan13Name())
			{
				outProfile = ShaderTargetProfile::GGLabVulkan13;
				return true;
			}
			return false;
		}

		// Report side: the stable set of supported wire names.
		[[nodiscard]] std::vector<std::string> Names() noexcept
		{
			std::vector<std::string> names;
			names.push_back(std::string(Dx12Name()));
			names.push_back(std::string(Vulkan13Name()));
			return names;
		}
	}
}
