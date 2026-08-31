#pragma once
#include "Graphics/GraphicsTypes.h"
#include "GGLabRuntime/Graphics/Shader/ShaderTypes.h"

#include <cstdint>

namespace gglab
{
	struct ShaderPipelineDependencyIdentity final
	{
		ShaderID m_ShaderId{};
		uint64_t m_Generation = 0;
		ShaderHash128 m_BinaryHash{};

		constexpr bool operator==(
			const ShaderPipelineDependencyIdentity&) const noexcept = default;
	};

	struct ShaderPipelineSnapshot final
	{
		ShaderPipelineDependencyIdentity m_Dependency{};
		ShaderBytecode m_Bytecode{};
	};
}
