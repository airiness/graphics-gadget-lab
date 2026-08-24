#pragma once
#include "ShaderArtifactRuntime/ShaderArtifactTypes.h"

#include <cstddef>
#include <string_view>

namespace gglab
{
	// Runtime bytecode view handed to the RHI backends. Points into the
	// ShaderArtifact binary owned by the runtime Shader object.
	struct ShaderBytecode
	{
		const void* m_Data = nullptr;
		size_t m_SizeInBytes = 0;
		ShaderBinaryFormat m_Format = ShaderBinaryFormat::Unknown;
		ShaderHash128 m_Hash{};
		std::string_view m_EntryPoint{};

		[[nodiscard]] bool IsValid() const noexcept
		{
			return m_Data != nullptr && m_SizeInBytes > 0;
		}
	};
}
