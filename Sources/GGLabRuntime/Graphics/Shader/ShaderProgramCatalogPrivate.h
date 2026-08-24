#pragma once
#include "Contracts/ShaderCompileTarget.h"
#include "Graphics/Shader/ShaderProgramCatalog.h"

#include <optional>

namespace gglab
{
	// Transitional in-process build adapter. Stable Runtime consumers never see
	// these authoring fields. This resolver is removed with the in-process compiler edge.
	[[nodiscard]] std::optional<ShaderDesc> ResolveTransitionalShaderProgramBuild(
		const ShaderProgramRef& programRef) noexcept;
}
