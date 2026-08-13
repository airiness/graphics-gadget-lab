#pragma once

#include <filesystem>

namespace gglab
{
	// Build deployment must expose this directory relative to the executable output.
	inline constexpr wchar_t ShaderSourceRelativeDirectory[] = L"Shaders";
	inline constexpr wchar_t ShaderCacheRelativeDirectory[] = L"ShaderCache";

	[[nodiscard]] std::filesystem::path ResolveShaderSourceRoot(
		const std::filesystem::path& runtimeRoot) noexcept;
	[[nodiscard]] std::filesystem::path ResolveShaderCacheRoot(
		const std::filesystem::path& runtimeRoot) noexcept;
}
