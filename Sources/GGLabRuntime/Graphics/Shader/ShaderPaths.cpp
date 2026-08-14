#include "Graphics/Shader/ShaderPaths.h"
#include "Core/Utility/PathUtils.h"

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
