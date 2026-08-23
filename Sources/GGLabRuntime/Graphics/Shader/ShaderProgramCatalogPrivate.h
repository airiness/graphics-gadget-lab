#pragma once
#include "Contracts/ShaderCompileTarget.h"
#include "Graphics/Shader/ShaderProgramCatalog.h"

#include <optional>

namespace gglab
{
	// Transitional in-process build adapter. Stable Runtime consumers never see
	// these authoring fields; R5 removes this resolver with the compiler edge.
	[[nodiscard]] std::optional<ShaderDesc> ResolveTransitionalShaderProgramBuild(
		const ShaderProgramRef& programRef) noexcept;
}
