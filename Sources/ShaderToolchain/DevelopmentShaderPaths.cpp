#include "DevelopmentShaderPaths.h"

namespace gglab
{
	std::filesystem::path ResolveShaderSourceRoot(
		const std::filesystem::path& runtimeRoot) noexcept
	{
		return runtimeRoot / "Shaders";
	}

	std::filesystem::path ResolveShaderCacheRoot(
		const std::filesystem::path& runtimeRoot) noexcept
	{
		return runtimeRoot / "ShaderCache";
	}
}
