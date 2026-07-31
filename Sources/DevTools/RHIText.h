#pragma once
#include "Graphics/RHI/RHIHandles.h"

#include <format>
#include <string>

namespace gglab::devtools
{
	template <typename Tag> [[nodiscard]] std::string RHIHandleText(RHIHandle<Tag> handle)
	{
		return handle.IsValid() ? std::format("{}:{}", handle.Index(), handle.Generation()) : "-";
	}
}
