#include "Core/Precompiled.h"
#include "Graphics/Shader/ShaderPaths.h"
#include "Core/Utility/PathUtils.h"

namespace gglab
{
	std::filesystem::path GetShaderSourceRoot() noexcept
	{
		return utils::Canonical(utils::GetExeOutDir() / ShaderSourceRelativeDirectory);
	}
}
