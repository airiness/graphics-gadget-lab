#pragma once
#include "ShaderArtifactRuntime/ShaderGraphPreviewProgram.h"
#include "ShaderArtifactRuntime/ShaderProgramRegistryArtifact.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace gglab
{
	inline constexpr uint32_t ShaderPreviewPublicationIdentitySchemaVersion = 1;
	inline constexpr uint32_t ShaderPreviewPublicationArtifactSchemaVersion = 1;
	inline constexpr uint32_t ShaderPreviewActivePublicationSchemaVersion = 1;
	inline constexpr uint32_t ShaderPreviewObservationSchemaVersion = 1;
	inline constexpr size_t MaxShaderPreviewIdentityComponentSize = 1024;
	inline constexpr size_t ShaderPreviewSessionIdSize = 32;
	inline constexpr std::string_view ShaderGraphPreviewSurfaceProfileId =
		"gglab.surface";

	struct ShaderPreviewPublicationId final
	{
		Sha256Digest m_DurableDigest{};

		[[nodiscard]] constexpr bool IsValid() const noexcept
		{
			return m_DurableDigest.IsValid();
		}

		friend constexpr bool operator==(
			const ShaderPreviewPublicationId&,
			const ShaderPreviewPublicationId&) noexcept = default;
	};

	struct ShaderPreviewPublicationRef final
	{
		ShaderPreviewPublicationId m_PublicationId{};

		[[nodiscard]] constexpr bool IsValid() const noexcept
		{
			return m_PublicationId.IsValid();
		}

		friend constexpr bool operator==(
			const ShaderPreviewPublicationRef&,
			const ShaderPreviewPublicationRef&) noexcept = default;
	};

	struct ShaderPreviewPublicationArtifact final
	{
		// Compiler-free persisted result. Source, compiler policy, diagnostics and
		// Runtime parameter values deliberately do not cross this boundary.
		uint32_t m_SchemaVersion = ShaderPreviewPublicationArtifactSchemaVersion;
		ShaderPreviewPublicationId m_PublicationId{};
		Sha256Digest m_PreviewProgramDescriptorIdentity{};
		std::string m_PreviewInputContractId{};
		std::string m_ProfileId{};
		uint32_t m_ProfileVersion = 0;
		Sha256Digest m_GeneratedSourceIdentity{};
		ShaderTargetProfile m_TargetProfile = ShaderTargetProfile::GGLabDX12;
		ShaderProgramRef m_ProgramRef{};
		ShaderArtifactRef m_ShaderArtifactRef{};
		ShaderProgramRegistryArtifactRef m_BaseRegistryRef{};
		ShaderProgramRegistryArtifactRef m_PreviewRegistryRef{};

		friend bool operator==(
			const ShaderPreviewPublicationArtifact&,
			const ShaderPreviewPublicationArtifact&) noexcept = default;
	};

	enum class ShaderPreviewPublicationValidationStatus : uint8_t
	{
		Valid,
		UnsupportedSchema,
		InvalidPublicationId,
		InvalidDescriptorIdentity,
		InvalidInputContract,
		InvalidProfile,
		InvalidGeneratedSourceIdentity,
		InvalidTarget,
		InvalidProgram,
		InvalidShaderArtifactRef,
		InvalidBaseRegistryRef,
		InvalidPreviewRegistryRef,
	};

	struct ShaderPreviewPublicationBuildResult final
	{
		ShaderPreviewPublicationValidationStatus m_Status =
			ShaderPreviewPublicationValidationStatus::InvalidPublicationId;
		ShaderPreviewPublicationArtifact m_Artifact{};

		[[nodiscard]] constexpr bool IsSuccess() const noexcept
		{
			return m_Status == ShaderPreviewPublicationValidationStatus::Valid;
		}
	};

	struct ShaderGraphPreviewInputContractProjection final
	{
		std::string_view m_InputContractId;
		uint32_t m_ProfileVersion = 0;
		const ShaderProgramRef* m_ProgramRef = nullptr;
	};

	[[nodiscard]] bool IsKnownShaderGraphPreviewProgramDescriptorIdentity(
		const Sha256Digest& identity) noexcept;
	[[nodiscard]] bool IsKnownShaderGraphPreviewInputContract(
		std::string_view inputContractId) noexcept;
	[[nodiscard]] const ShaderGraphPreviewInputContractProjection*
		ResolveShaderGraphPreviewInputContract(
			std::string_view inputContractId) noexcept;
	[[nodiscard]] bool IsKnownShaderGraphPreviewProgramRef(
		const ShaderProgramRef& programRef) noexcept;
	[[nodiscard]] ShaderPreviewPublicationId ComputeShaderPreviewPublicationId(
		const ShaderPreviewPublicationArtifact& artifact) noexcept;
	[[nodiscard]] ShaderPreviewPublicationValidationStatus
		ValidateShaderPreviewPublicationArtifact(
			const ShaderPreviewPublicationArtifact& artifact) noexcept;
	[[nodiscard]] ShaderPreviewPublicationBuildResult
		BuildShaderPreviewPublicationArtifact(
			ShaderPreviewPublicationArtifact artifact) noexcept;

	enum class ShaderPreviewRegistryOverlayBuildStatus : uint8_t
	{
		Built,
		InvalidBaseRegistry,
		InvalidProgram,
		InvalidTarget,
		InvalidArtifact,
		MissingBinding,
		Failed,
	};

	struct ShaderPreviewRegistryOverlayBuildResult final
	{
		ShaderPreviewRegistryOverlayBuildStatus m_Status =
			ShaderPreviewRegistryOverlayBuildStatus::Failed;
		ShaderProgramRegistryArtifact m_Artifact{};

		[[nodiscard]] constexpr bool IsSuccess() const noexcept
		{
			return m_Status == ShaderPreviewRegistryOverlayBuildStatus::Built;
		}
	};

	// Produces one complete immutable snapshot by replacing exactly the selected
	// Preview binding. The ordinary active Registry pointer is not part of this API.
	[[nodiscard]] ShaderPreviewRegistryOverlayBuildResult
		BuildShaderPreviewRegistryOverlay(
			const ShaderProgramRegistryArtifact& baseRegistry,
			const ShaderProgramRef& programRef,
			ShaderTargetProfile targetProfile,
			const ShaderArtifactRef& artifactRef) noexcept;

	enum class ShaderPreviewPublicationLinkValidationStatus : uint8_t
	{
		Valid,
		InvalidPublication,
		InvalidShaderArtifact,
		InvalidBaseRegistry,
		InvalidPreviewRegistry,
		ArtifactContractMismatch,
		RegistryShapeMismatch,
		PreviewBindingMismatch,
		NonPreviewBindingChanged,
	};

	// Cross-validates already-read compiler-free products. Callers remain
	// responsible for resolving the three refs through their artifact readers.
	[[nodiscard]] ShaderPreviewPublicationLinkValidationStatus
		ValidateShaderPreviewPublicationLinks(
			const ShaderPreviewPublicationArtifact& publication,
			const ShaderRuntimeArtifactManifest& shaderArtifactManifest,
			const ShaderProgramRegistryArtifact& baseRegistry,
			const ShaderProgramRegistryArtifact& previewRegistry) noexcept;

	[[nodiscard]] constexpr bool IsValidShaderPreviewSessionId(
		std::string_view sessionId) noexcept
	{
		if (sessionId.size() != ShaderPreviewSessionIdSize)
		{
			return false;
		}
		for (const char character : sessionId)
		{
			if (!((character >= '0' && character <= '9') ||
				(character >= 'a' && character <= 'f')))
			{
				return false;
			}
		}
		return true;
	}

	struct ShaderPreviewActivePublication final
	{
		// Mutable session commit record. Session identity belongs to the validated
		// locator path and is intentionally not duplicated in this payload.
		uint32_t m_SchemaVersion = ShaderPreviewActivePublicationSchemaVersion;
		uint64_t m_AttemptSequence = 0;
		ShaderPreviewPublicationRef m_PublicationRef{};

		friend constexpr bool operator==(
			const ShaderPreviewActivePublication&,
			const ShaderPreviewActivePublication&) noexcept = default;
	};

	[[nodiscard]] constexpr bool IsValidShaderPreviewActivePublication(
		const ShaderPreviewActivePublication& activePublication) noexcept
	{
		return activePublication.m_SchemaVersion ==
				ShaderPreviewActivePublicationSchemaVersion &&
			activePublication.m_PublicationRef.IsValid();
	}

	enum class ShaderPreviewActivePublicationOrderingStatus : uint8_t
	{
		Publishable,
		InvalidCandidate,
		InvalidCurrent,
		NotNewer,
	};

	[[nodiscard]] ShaderPreviewActivePublicationOrderingStatus
		ValidateShaderPreviewActivePublicationOrdering(
			const ShaderPreviewActivePublication* current,
			const ShaderPreviewActivePublication& candidate) noexcept;

	enum class ShaderPreviewObservationStatus : uint8_t
	{
		Loaded = 1,
		Rejected = 2,
	};

	enum class ShaderPreviewRejectionCode : uint8_t
	{
		None = 0,
		PublicationUnavailable = 1,
		PublicationInvalid = 2,
		ShaderArtifactUnavailable = 3,
		ShaderArtifactInvalid = 4,
		RegistryUnavailable = 5,
		RegistryInvalid = 6,
		ActivationFailed = 7,
		IOFailure = 8,
	};

	struct ShaderPreviewObservation final
	{
		// Mutable Runtime observation. Rejection remains structured and bounded;
		// diagnostics prose is process-local and never persisted here.
		uint32_t m_SchemaVersion = ShaderPreviewObservationSchemaVersion;
		uint64_t m_ObservedAttemptSequence = 0;
		ShaderPreviewPublicationRef m_ObservedPublicationRef{};
		ShaderPreviewPublicationRef m_LoadedPublicationRef{};
		ShaderPreviewObservationStatus m_Status = ShaderPreviewObservationStatus::Rejected;
		ShaderPreviewRejectionCode m_RejectionCode =
			ShaderPreviewRejectionCode::PublicationUnavailable;

		friend constexpr bool operator==(
			const ShaderPreviewObservation&,
			const ShaderPreviewObservation&) noexcept = default;
	};

	[[nodiscard]] bool IsKnownShaderPreviewRejectionCode(
		ShaderPreviewRejectionCode rejectionCode) noexcept;
	[[nodiscard]] bool IsValidShaderPreviewObservation(
		const ShaderPreviewObservation& observation) noexcept;

	enum class ShaderPreviewObservationOrderingStatus : uint8_t
	{
		Publishable,
		InvalidCandidate,
		InvalidCurrent,
		NotNewer,
		LastGoodChangedOnRejection,
	};

	[[nodiscard]] ShaderPreviewObservationOrderingStatus
		ValidateShaderPreviewObservationOrdering(
			const ShaderPreviewObservation* current,
			const ShaderPreviewObservation& candidate) noexcept;
}
