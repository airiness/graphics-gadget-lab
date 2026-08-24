#pragma once
#include "ShaderArtifactRuntime/ShaderArtifactTypes.h"

#include <cstddef>
#include <string_view>

namespace gglab
{
	// Runtime bytecode view handed to the RHI backends. It points into the
	// ShaderArtifact binary owned by the runtime Shader object and is borrowed
	// only for the duration of the synchronous pipeline-creation call.
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
