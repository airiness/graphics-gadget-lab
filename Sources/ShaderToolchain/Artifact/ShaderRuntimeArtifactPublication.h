#pragma once
#include "Contracts/ShaderArtifact.h"
#include "ShaderArtifactRuntime/ShaderLooseArtifactIO.h"
#include "ShaderArtifactRuntime/ShaderPreviewLooseIO.h"

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

	enum class ShaderProgramRegistryArtifactPublicationStatus : uint8_t
	{
		Published,
		AlreadyPresent,
		InvalidArtifact,
		IOFailure,
	};

	struct ShaderProgramRegistryArtifactPublicationResult final
	{
		ShaderProgramRegistryArtifactPublicationStatus m_Status =
			ShaderProgramRegistryArtifactPublicationStatus::IOFailure;
		ShaderProgramRegistryArtifactRef m_RegistryRef{};
		ShaderLooseProgramRegistryArtifactPath m_Path{};

		[[nodiscard]] constexpr bool IsSuccess() const noexcept
		{
			return m_Status == ShaderProgramRegistryArtifactPublicationStatus::Published ||
				m_Status == ShaderProgramRegistryArtifactPublicationStatus::AlreadyPresent;
		}
	};

	enum class ActiveShaderProgramRegistryPublicationStatus : uint8_t
	{
		Published,
		AlreadyActive,
		InvalidRegistry,
		IOFailure,
	};

	struct ActiveShaderProgramRegistryPublicationResult final
	{
		ActiveShaderProgramRegistryPublicationStatus m_Status =
			ActiveShaderProgramRegistryPublicationStatus::IOFailure;
		ShaderProgramRegistryArtifactRef m_RegistryRef{};
		std::filesystem::path m_Path{};

		[[nodiscard]] constexpr bool IsSuccess() const noexcept
		{
			return m_Status == ActiveShaderProgramRegistryPublicationStatus::Published ||
				m_Status == ActiveShaderProgramRegistryPublicationStatus::AlreadyActive;
		}
	};

	enum class ShaderPreviewPublicationArtifactPublicationStatus : uint8_t
	{
		Published,
		AlreadyPresent,
		InvalidArtifact,
		IOFailure,
	};

	struct ShaderPreviewPublicationArtifactPublicationResult final
	{
		ShaderPreviewPublicationArtifactPublicationStatus m_Status =
			ShaderPreviewPublicationArtifactPublicationStatus::IOFailure;
		ShaderPreviewPublicationRef m_PublicationRef{};
		ShaderLoosePreviewPublicationPath m_Path{};

		[[nodiscard]] constexpr bool IsSuccess() const noexcept
		{
			return m_Status ==
					ShaderPreviewPublicationArtifactPublicationStatus::Published ||
				m_Status ==
					ShaderPreviewPublicationArtifactPublicationStatus::AlreadyPresent;
		}
	};

	enum class ShaderPreviewActivePublicationPublicationStatus : uint8_t
	{
		Published,
		AlreadyActive,
		InvalidCandidate,
		InvalidCurrent,
		NotNewer,
		PublicationUnavailable,
		IOFailure,
	};

	struct ShaderPreviewActivePublicationPublicationResult final
	{
		ShaderPreviewActivePublicationPublicationStatus m_Status =
			ShaderPreviewActivePublicationPublicationStatus::IOFailure;
		ShaderPreviewActivePublication m_ActivePublication{};
		std::filesystem::path m_Path{};
		// Meaningful for Published: false records that the best-effort
		// post-commit read did not observe the committed value. MoveFileExW is
		// still the authoritative commit point and its success is never reported
		// as an I/O failure.
		bool m_PostCommitObservationSucceeded = false;

		[[nodiscard]] constexpr bool IsSuccess() const noexcept
		{
			return m_Status ==
					ShaderPreviewActivePublicationPublicationStatus::Published ||
				m_Status ==
					ShaderPreviewActivePublicationPublicationStatus::AlreadyActive;
		}
	};

	[[nodiscard]] ShaderRuntimeArtifact BuildShaderRuntimeArtifact(
		const ShaderArtifact& artifact);

	// Publishes immutable content-addressed Runtime files. Binary is published
	// first; the versioned Runtime manifest is published last as the logical commit
	// record. Success is based on final Store validation, never exists alone.
	[[nodiscard]] ShaderRuntimeArtifactPublicationResult PublishShaderRuntimeArtifact(
		const std::filesystem::path& artifactRoot,
		const ShaderArtifact& artifact) noexcept;

	// Publishes one immutable, content-addressed Program Registry snapshot.
	// Active-snapshot selection and multi-writer update policy are separate contracts.
	[[nodiscard]] ShaderProgramRegistryArtifactPublicationResult
		PublishShaderProgramRegistryArtifact(
			const std::filesystem::path& artifactRoot,
			const ShaderProgramRegistryArtifact& artifact) noexcept;

	// Atomically selects one already-published immutable Program Registry.
	// The caller must hold the artifact-root writer lease across the complete
	// build and this final commit. Readers never take that lease.
	[[nodiscard]] ActiveShaderProgramRegistryPublicationResult
		PublishActiveShaderProgramRegistry(
			const std::filesystem::path& artifactRoot,
			ShaderTargetProfile targetProfile,
			const ShaderProgramRegistryArtifactRef& registryRef) noexcept;

	// Publishes one immutable content-addressed Preview Publication. The caller
	// owns complete cross-link validation before making this product reachable.
	[[nodiscard]] ShaderPreviewPublicationArtifactPublicationResult
		PublishShaderPreviewPublicationArtifact(
			const std::filesystem::path& artifactRoot,
			const ShaderPreviewPublicationArtifact& artifact) noexcept;

	// Atomically advances one Preview session after proving that the immutable
	// Publication exists. The caller must hold the artifact-root writer lease.
	[[nodiscard]] ShaderPreviewActivePublicationPublicationResult
		PublishShaderPreviewActivePublication(
			const std::filesystem::path& artifactRoot,
			std::string sessionId,
			const ShaderPreviewActivePublication& activePublication) noexcept;
}
