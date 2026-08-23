#pragma once
#include "Contracts/ShaderArtifact.h"
#include "ShaderArtifactRuntime/ShaderLooseArtifactIO.h"

#include <cstdint>
#include <filesystem>

namespace gglab
{
	enum class ShaderRuntimeArtifactPublicationStatus : uint8_t
	{
		Published,
		AlreadyPresent,
		InvalidArtifact,
		IOFailure,
	};

	struct ShaderRuntimeArtifactPublicationResult final
	{
		ShaderRuntimeArtifactPublicationStatus m_Status =
			ShaderRuntimeArtifactPublicationStatus::IOFailure;
		ShaderArtifactRef m_ArtifactRef{};
		ShaderLooseArtifactPaths m_Paths{};

		[[nodiscard]] constexpr bool IsSuccess() const noexcept
		{
			return m_Status == ShaderRuntimeArtifactPublicationStatus::Published ||
				m_Status == ShaderRuntimeArtifactPublicationStatus::AlreadyPresent;
		}
	};

	[[nodiscard]] ShaderRuntimeArtifact BuildShaderRuntimeArtifact(
		const ShaderArtifact& artifact);

	// Publishes immutable content-addressed Runtime files. Binary is published
	// first; the fixed Runtime manifest is published last as the logical commit
	// record. Success is based on final Store validation, never exists alone.
	[[nodiscard]] ShaderRuntimeArtifactPublicationResult PublishShaderRuntimeArtifact(
		const std::filesystem::path& artifactRoot,
		const ShaderArtifact& artifact) noexcept;
}
