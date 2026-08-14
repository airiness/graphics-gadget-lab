#pragma once
#include "Core/StringId.h"

#include <string>

namespace gglab::utils
{
	[[nodiscard]] std::string StringIdToString(StringID id) noexcept;
}
