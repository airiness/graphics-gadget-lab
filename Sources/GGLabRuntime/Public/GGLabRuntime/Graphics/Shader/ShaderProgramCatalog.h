#pragma once
#include "ShaderArtifactRuntime/GGLabShaderPrograms.h"

#include <span>

namespace gglab::shader_programs
{
	[[nodiscard]] std::span<const ShaderProgramRef>
		GetForwardPBRMaterialDiagnosticsShaderProgramDemand() noexcept;

	[[nodiscard]] std::span<const ShaderProgramRef>
		GetRendererInitialShaderProgramDemand() noexcept;
}
