#include "Graphics/Shader/ShaderPaths.h"
#include "GGLabFoundation/IO/PathUtils.h"

namespace gglab
{
	std::filesystem::path ResolveShaderSourceRoot(
		const std::filesystem::path& runtimeRoot) noexcept
	{
		return utils::Canonical(runtimeRoot / ShaderSourceRelativeDirectory);
	}

	std::filesystem::path ResolveShaderCacheRoot(
		const std::filesystem::path& runtimeRoot) noexcept
	{
		return utils::Canonical(runtimeRoot / ShaderCacheRelativeDirectory);
	}
}
