#pragma once
#include "ShaderArtifactRuntime/ShaderProgramRegistryArtifact.h"

#include <cstdint>
#include <filesystem>
#include <string>

namespace gglab
{
	enum class GGLabRuntimeShaderBuildStatus : uint8_t
	{
		Succeeded,
		InvalidInput,
		WriterUnavailable,
		CompilerUnavailable,
		CompileFailed,
		ArtifactPublicationFailed,
		RegistryBuildFailed,
		RegistryPublicationFailed,
		ActiveRegistryPublicationFailed,
		Failed,
	};

	struct GGLabRuntimeShaderBuildResult final
	{
		GGLabRuntimeShaderBuildStatus m_Status = GGLabRuntimeShaderBuildStatus::Failed;
		ShaderProgramRegistryArtifactRef m_RegistryRef{};
		uint32_t m_ProgramCount = 0;
		std::string m_Error{};

		[[nodiscard]] bool IsSuccess() const noexcept
		{
			return m_Status == GGLabRuntimeShaderBuildStatus::Succeeded &&
				m_RegistryRef.IsValid() && m_ProgramCount > 0;
		}
	};

	// Shared artifact-root writer identity for every Toolchain publication
	// transaction, including ordinary Runtime and Preview builds.
	[[nodiscard]] std::wstring MakeGGLabShaderArtifactWriterMutexName(
		const std::filesystem::path& artifactRoot) noexcept;

	[[nodiscard]] GGLabRuntimeShaderBuildResult BuildGGLabRuntimeShaders(
		ShaderTargetProfile targetProfile,
		const std::filesystem::path& sourceRoot,
		const std::filesystem::path& cacheRoot,
		const std::filesystem::path& artifactRoot) noexcept;
}
