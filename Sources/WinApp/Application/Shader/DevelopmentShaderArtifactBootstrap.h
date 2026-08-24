#pragma once
#include "Graphics/RHI/RHITypes.h"
#include "ShaderArtifactRuntime/ShaderProgramRegistryArtifact.h"

#include <cstdint>
#include <filesystem>
#include <string>

namespace gglab
{
	enum class DevelopmentShaderArtifactBootstrapStatus : uint8_t
	{
		Succeeded,
		InvalidInput,
		CompilerUnavailable,
		UnknownProgram,
		CompileFailed,
		ArtifactPublicationFailed,
		RegistryBuildFailed,
		RegistryPublicationFailed,
		Failed,
	};

	struct DevelopmentShaderArtifactBootstrapResult final
	{
		DevelopmentShaderArtifactBootstrapStatus m_Status =
			DevelopmentShaderArtifactBootstrapStatus::Failed;
		ShaderProgramRegistryArtifactRef m_RegistryRef{};
		std::string m_Error;

		[[nodiscard]] bool IsSuccess() const noexcept
		{
			return m_Status == DevelopmentShaderArtifactBootstrapStatus::Succeeded &&
				m_RegistryRef.IsValid();
		}
	};

	// Transitional desktop producer used until the external shader build bridge
	// owns development publication. Runtime consumes only the returned immutable ref.
	[[nodiscard]] DevelopmentShaderArtifactBootstrapResult
		PrepareDevelopmentShaderArtifacts(
			RHIBackendType activeBackend,
			const std::filesystem::path& shaderSourceRoot,
			const std::filesystem::path& shaderCacheRoot,
			const std::filesystem::path& artifactRoot) noexcept;
}
