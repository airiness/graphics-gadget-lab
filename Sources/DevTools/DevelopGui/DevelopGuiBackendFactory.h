#pragma once
#include "DevTools/DevelopGui/DevelopGuiBackend.h"

#include <memory>

namespace gglab
{
	[[nodiscard]] std::unique_ptr<DevelopGuiBackend> CreateDevelopGuiBackend(
		const DevelopGuiBackend::CreateInfo& createInfo) noexcept;
}
