#include "GGLabRuntime/Core/StringIdFormatting.h"

#include <format>
#include <string_view>

namespace gglab::utils
{
	std::string StringIdToString(StringID id) noexcept
	{
		if (id.Value() == 0)
		{
			return {};
		}

		const std::string_view name = id.Name();
		return name.empty() ? std::format("0x{:016X}", id.Value()) : std::string(name);
	}
}
