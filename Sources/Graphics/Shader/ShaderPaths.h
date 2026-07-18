#pragma once

#include <filesystem>

namespace gglab
{
	// Build deployment must expose this directory relative to the executable output.
	inline constexpr wchar_t ShaderSourceRelativeDirectory[] = L"Assets/Shaders";

	[[nodiscard]] std::filesystem::path GetShaderSourceRoot() noexcept;
}
