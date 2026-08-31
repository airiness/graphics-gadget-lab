#pragma once

#include "ShaderArtifactRuntime/ShaderPreviewPublication.h"

#include <cstdint>
#include <filesystem>
#include <string>

namespace gglab
{
	inline constexpr uintmax_t MaxShaderPreviewGeneratedSourceSize =
		4u * 1024u * 1024u;

	struct GGLabShaderPreviewBuildRequest final
	{
		std::filesystem::path m_SourceRoot{};
		std::filesystem::path m_GeneratedSourcePath{};
		std::filesystem::path m_CacheRoot{};
		std::filesystem::path m_ArtifactRoot{};
		std::string m_SessionId{};
		ShaderTargetProfile m_TargetProfile = ShaderTargetProfile::GGLabDX12;
		std::string m_ProfileId{};
		uint32_t m_ProfileVersion = 0;
		std::string m_PreviewInputContractId{};
		Sha256Digest m_PreviewProgramDescriptorIdentity{};
		Sha256Digest m_GeneratedSourceIdentity{};
		uint64_t m_AttemptSequence = 0;
	};

	enum class GGLabShaderPreviewBuildStatus : uint8_t
	{
		Succeeded,
		InvalidInput,
		GeneratedSourceUnavailable,
		GeneratedSourceIdentityMismatch,
		WriterUnavailable,
		BaseRegistryUnavailable,
		CompilerUnavailable,
		CompileFailed,
		ArtifactPublicationFailed,
		RegistryBuildFailed,
		RegistryPublicationFailed,
		PublicationBuildFailed,
		PublicationValidationFailed,
		PublicationPublicationFailed,
		StaleAttempt,
		ActivePublicationFailed,
		Failed,
	};

	struct GGLabShaderPreviewBuildResult final
	{
		GGLabShaderPreviewBuildStatus m_Status =
			GGLabShaderPreviewBuildStatus::Failed;
		uint64_t m_AttemptSequence = 0;
		ShaderPreviewPublicationRef m_PublicationRef{};
		ShaderArtifactRef m_ShaderArtifactRef{};
		ShaderProgramRegistryArtifactRef m_BaseRegistryRef{};
		ShaderProgramRegistryArtifactRef m_PreviewRegistryRef{};
		std::string m_Error{};

		[[nodiscard]] constexpr bool IsSuccess() const noexcept
		{
			return m_Status == GGLabShaderPreviewBuildStatus::Succeeded &&
				m_PublicationRef.IsValid() && m_ShaderArtifactRef.IsValid() &&
				m_BaseRegistryRef.IsValid() && m_PreviewRegistryRef.IsValid();
		}
	};

	[[nodiscard]] GGLabShaderPreviewBuildResult BuildGGLabShaderPreview(
		const GGLabShaderPreviewBuildRequest& request) noexcept;
}
