#include "ShaderArtifactRuntime/ShaderPreviewPublication.h"

#include "ShaderArtifactRuntime/GGLabShaderPrograms.h"
#include "ShaderArtifactRuntime/ShaderArtifactStore.h"

#include <algorithm>
#include <array>
#include <span>
#include <utility>
#include <vector>

namespace gglab
{
	namespace
	{
		struct PreviewInputContractProjection final
		{
			std::string_view m_InputContractId;
			uint32_t m_ProfileVersion = 0;
			const ShaderProgramRef* m_ProgramRef = nullptr;
		};

		const std::array PreviewInputContracts{
			PreviewInputContractProjection{
				ShaderGraphPreviewNumericInputContractId,
				1,
				&shader_programs::ShaderGraphPreviewSurfaceV1Pixel,
			},
			PreviewInputContractProjection{
				ShaderGraphPreviewTexture2DInputContractId,
				2,
				&shader_programs::ShaderGraphPreviewSurfaceV2Pixel,
			},
		};

		[[nodiscard]] const PreviewInputContractProjection* FindInputContract(
			std::string_view inputContractId) noexcept
		{
			const auto iterator = std::ranges::find(
				PreviewInputContracts, inputContractId,
				&PreviewInputContractProjection::m_InputContractId);
			return iterator != PreviewInputContracts.end() ? &*iterator : nullptr;
		}

		[[nodiscard]] bool DigestMatchesLowerHex(
			const Sha256Digest& digest, std::string_view lowerHex) noexcept
		{
			if (lowerHex.size() != Sha256Digest::Size * 2)
			{
				return false;
			}
			constexpr char HexDigits[] = "0123456789abcdef";
			for (size_t index = 0; index < digest.m_Value.size(); ++index)
			{
				const uint8_t value = std::to_integer<uint8_t>(digest.m_Value[index]);
				if (lowerHex[index * 2] != HexDigits[value >> 4] ||
					lowerHex[index * 2 + 1] != HexDigits[value & 0x0fu])
				{
					return false;
				}
			}
			return true;
		}

		[[nodiscard]] ShaderPreviewPublicationValidationStatus ValidatePayload(
			const ShaderPreviewPublicationArtifact& artifact) noexcept
		{
			if (!IsKnownShaderGraphPreviewProgramDescriptorIdentity(
				artifact.m_PreviewProgramDescriptorIdentity))
			{
				return ShaderPreviewPublicationValidationStatus::InvalidDescriptorIdentity;
			}
			const PreviewInputContractProjection* inputContract =
				FindInputContract(artifact.m_PreviewInputContractId);
			if (!inputContract)
			{
				return ShaderPreviewPublicationValidationStatus::InvalidInputContract;
			}
			if (artifact.m_ProfileId != ShaderGraphPreviewSurfaceProfileId ||
				artifact.m_ProfileVersion != inputContract->m_ProfileVersion)
			{
				return ShaderPreviewPublicationValidationStatus::InvalidProfile;
			}
			if (!artifact.m_GeneratedSourceIdentity.IsValid())
			{
				return ShaderPreviewPublicationValidationStatus::InvalidGeneratedSourceIdentity;
			}
			if (!IsKnownShaderTargetProfile(artifact.m_TargetProfile))
			{
				return ShaderPreviewPublicationValidationStatus::InvalidTarget;
			}
			if (artifact.m_ProgramRef != *inputContract->m_ProgramRef)
			{
				return ShaderPreviewPublicationValidationStatus::InvalidProgram;
			}
			if (!artifact.m_ShaderArtifactRef.IsValid())
			{
				return ShaderPreviewPublicationValidationStatus::InvalidShaderArtifactRef;
			}
			if (!artifact.m_BaseRegistryRef.IsValid())
			{
				return ShaderPreviewPublicationValidationStatus::InvalidBaseRegistryRef;
			}
			if (!artifact.m_PreviewRegistryRef.IsValid())
			{
				return ShaderPreviewPublicationValidationStatus::InvalidPreviewRegistryRef;
			}
			return ShaderPreviewPublicationValidationStatus::Valid;
		}

		[[nodiscard]] bool HasSameBinding(
			const ShaderProgramRegistryEntry& left,
			const ShaderProgramRegistryEntry& right) noexcept
		{
			return left.m_ProgramRef == right.m_ProgramRef &&
				left.m_TargetProfile == right.m_TargetProfile;
		}
	}

	bool IsKnownShaderGraphPreviewProgramDescriptorIdentity(
		const Sha256Digest& identity) noexcept
	{
		return identity.IsValid() && DigestMatchesLowerHex(
			identity, ShaderGraphPreviewProgramDescriptorIdentity);
	}

	bool IsKnownShaderGraphPreviewInputContract(
		std::string_view inputContractId) noexcept
	{
		return FindInputContract(inputContractId) != nullptr;
	}

	bool IsKnownShaderGraphPreviewProgramRef(
		const ShaderProgramRef& programRef) noexcept
	{
		return std::ranges::any_of(
			PreviewInputContracts,
			[&programRef](const PreviewInputContractProjection& projection) noexcept
			{ return programRef == *projection.m_ProgramRef; });
	}

	ShaderPreviewPublicationId ComputeShaderPreviewPublicationId(
		const ShaderPreviewPublicationArtifact& artifact) noexcept
	{
		if (ValidatePayload(artifact) != ShaderPreviewPublicationValidationStatus::Valid)
		{
			return {};
		}

		Sha256Builder builder;
		const bool succeeded =
			builder.AddStringUtf8("gglab.shader.preview-publication-id") &&
			builder.AddU32LE(ShaderPreviewPublicationIdentitySchemaVersion) &&
			builder.AddU32LE(ShaderPreviewPublicationArtifactSchemaVersion) &&
			builder.AddBytes(artifact.m_PreviewProgramDescriptorIdentity.m_Value) &&
			builder.AddStringUtf8(artifact.m_PreviewInputContractId) &&
			builder.AddStringUtf8(artifact.m_ProfileId) &&
			builder.AddU32LE(artifact.m_ProfileVersion) &&
			builder.AddBytes(artifact.m_GeneratedSourceIdentity.m_Value) &&
			builder.AddU8(static_cast<uint8_t>(artifact.m_TargetProfile)) &&
			builder.AddStringUtf8(artifact.m_ProgramRef.m_ProgramId) &&
			builder.AddStringUtf8(artifact.m_ProgramRef.m_VariantId) &&
			builder.AddU32LE(static_cast<uint32_t>(artifact.m_ProgramRef.m_Stage)) &&
			builder.AddBytes(
				artifact.m_ShaderArtifactRef.m_ArtifactId.m_DurableDigest.m_Value) &&
			builder.AddBytes(
				artifact.m_BaseRegistryRef.m_RegistryId.m_DurableDigest.m_Value) &&
			builder.AddBytes(
				artifact.m_PreviewRegistryRef.m_RegistryId.m_DurableDigest.m_Value);
		return succeeded
			? ShaderPreviewPublicationId{ .m_DurableDigest = builder.Finish() }
			: ShaderPreviewPublicationId{};
	}

	ShaderPreviewPublicationValidationStatus ValidateShaderPreviewPublicationArtifact(
		const ShaderPreviewPublicationArtifact& artifact) noexcept
	{
		if (artifact.m_SchemaVersion != ShaderPreviewPublicationArtifactSchemaVersion)
		{
			return ShaderPreviewPublicationValidationStatus::UnsupportedSchema;
		}
		if (!artifact.m_PublicationId.IsValid())
		{
			return ShaderPreviewPublicationValidationStatus::InvalidPublicationId;
		}
		const ShaderPreviewPublicationValidationStatus payloadStatus =
			ValidatePayload(artifact);
		if (payloadStatus != ShaderPreviewPublicationValidationStatus::Valid)
		{
			return payloadStatus;
		}
		return ComputeShaderPreviewPublicationId(artifact) == artifact.m_PublicationId
			? ShaderPreviewPublicationValidationStatus::Valid
			: ShaderPreviewPublicationValidationStatus::InvalidPublicationId;
	}

	ShaderPreviewPublicationBuildResult BuildShaderPreviewPublicationArtifact(
		ShaderPreviewPublicationArtifact artifact) noexcept
	{
		artifact.m_SchemaVersion = ShaderPreviewPublicationArtifactSchemaVersion;
		artifact.m_PublicationId = {};
		const ShaderPreviewPublicationValidationStatus payloadStatus =
			ValidatePayload(artifact);
		if (payloadStatus != ShaderPreviewPublicationValidationStatus::Valid)
		{
			return { .m_Status = payloadStatus };
		}
		artifact.m_PublicationId = ComputeShaderPreviewPublicationId(artifact);
		const ShaderPreviewPublicationValidationStatus status =
			ValidateShaderPreviewPublicationArtifact(artifact);
		return {
			.m_Status = status,
			.m_Artifact = status == ShaderPreviewPublicationValidationStatus::Valid
				? std::move(artifact)
				: ShaderPreviewPublicationArtifact{},
		};
	}

	ShaderPreviewRegistryOverlayBuildResult BuildShaderPreviewRegistryOverlay(
		const ShaderProgramRegistryArtifact& baseRegistry,
		const ShaderProgramRef& programRef,
		ShaderTargetProfile targetProfile,
		const ShaderArtifactRef& artifactRef) noexcept
	{
		if (ValidateShaderProgramRegistryArtifact(baseRegistry) !=
			ShaderProgramRegistryArtifactValidationStatus::Valid)
		{
			return { .m_Status =
				ShaderPreviewRegistryOverlayBuildStatus::InvalidBaseRegistry };
		}
		if (!IsKnownShaderGraphPreviewProgramRef(programRef))
		{
			return { .m_Status = ShaderPreviewRegistryOverlayBuildStatus::InvalidProgram };
		}
		if (!IsKnownShaderTargetProfile(targetProfile))
		{
			return { .m_Status = ShaderPreviewRegistryOverlayBuildStatus::InvalidTarget };
		}
		if (!artifactRef.IsValid())
		{
			return { .m_Status = ShaderPreviewRegistryOverlayBuildStatus::InvalidArtifact };
		}

		try
		{
			std::vector<ShaderProgramRegistryEntry> entries = baseRegistry.m_Entries;
			const auto iterator = std::ranges::find_if(
				entries,
				[&](const ShaderProgramRegistryEntry& entry) noexcept
				{
					return entry.m_ProgramRef == programRef &&
						entry.m_TargetProfile == targetProfile;
				});
			if (iterator == entries.end())
			{
				return { .m_Status = ShaderPreviewRegistryOverlayBuildStatus::MissingBinding };
			}
			iterator->m_ArtifactRef = artifactRef;
			ShaderProgramRegistryArtifactBuildResult build =
				BuildShaderProgramRegistryArtifact(entries);
			return build.IsSuccess()
				? ShaderPreviewRegistryOverlayBuildResult{
					.m_Status = ShaderPreviewRegistryOverlayBuildStatus::Built,
					.m_Artifact = std::move(build.m_Artifact),
				}
				: ShaderPreviewRegistryOverlayBuildResult{
					.m_Status = ShaderPreviewRegistryOverlayBuildStatus::Failed,
				};
		}
		catch (...)
		{
			return { .m_Status = ShaderPreviewRegistryOverlayBuildStatus::Failed };
		}
	}

	ShaderPreviewPublicationLinkValidationStatus ValidateShaderPreviewPublicationLinks(
		const ShaderPreviewPublicationArtifact& publication,
		const ShaderRuntimeArtifactManifest& shaderArtifactManifest,
		const ShaderProgramRegistryArtifact& baseRegistry,
		const ShaderProgramRegistryArtifact& previewRegistry) noexcept
	{
		if (ValidateShaderPreviewPublicationArtifact(publication) !=
			ShaderPreviewPublicationValidationStatus::Valid)
		{
			return ShaderPreviewPublicationLinkValidationStatus::InvalidPublication;
		}
		if (!shaderArtifactManifest.m_ArtifactId.IsValid() ||
			shaderArtifactManifest.m_ArtifactId !=
				publication.m_ShaderArtifactRef.m_ArtifactId ||
			ComputeShaderArtifactId(shaderArtifactManifest) !=
				shaderArtifactManifest.m_ArtifactId)
		{
			return ShaderPreviewPublicationLinkValidationStatus::InvalidShaderArtifact;
		}
		if (ValidateShaderProgramRegistryArtifact(baseRegistry) !=
			ShaderProgramRegistryArtifactValidationStatus::Valid ||
			baseRegistry.m_RegistryId != publication.m_BaseRegistryRef.m_RegistryId)
		{
			return ShaderPreviewPublicationLinkValidationStatus::InvalidBaseRegistry;
		}
		if (ValidateShaderProgramRegistryArtifact(previewRegistry) !=
			ShaderProgramRegistryArtifactValidationStatus::Valid ||
			previewRegistry.m_RegistryId != publication.m_PreviewRegistryRef.m_RegistryId)
		{
			return ShaderPreviewPublicationLinkValidationStatus::InvalidPreviewRegistry;
		}
		if (shaderArtifactManifest.m_TargetProfile != publication.m_TargetProfile ||
			!ValidateShaderArtifactCompatibility(
				shaderArtifactManifest,
				ShaderArtifactCompatibilityRequest{
					.m_TargetProfile = publication.m_TargetProfile,
					.m_BinaryFormat = shaderArtifactManifest.m_BinaryFormat,
					.m_SpirVTargetEnvironment =
						shaderArtifactManifest.m_SpirVTargetEnvironment,
					.m_BindingABIRevision = shaderArtifactManifest.m_BindingABIRevision,
					.m_CoordinateOptions = shaderArtifactManifest.m_CoordinateOptions,
					.m_Stage = ShaderStage::Pixel,
				}).IsCompatible() ||
			shaderArtifactManifest.m_EntryPoint != ShaderGraphPreviewProgramEntry)
		{
			return ShaderPreviewPublicationLinkValidationStatus::ArtifactContractMismatch;
		}
		if (baseRegistry.m_Entries.size() != previewRegistry.m_Entries.size())
		{
			return ShaderPreviewPublicationLinkValidationStatus::RegistryShapeMismatch;
		}

		bool foundPreviewBinding = false;
		for (size_t index = 0; index < baseRegistry.m_Entries.size(); ++index)
		{
			const ShaderProgramRegistryEntry& baseEntry = baseRegistry.m_Entries[index];
			const ShaderProgramRegistryEntry& previewEntry = previewRegistry.m_Entries[index];
			if (!HasSameBinding(baseEntry, previewEntry))
			{
				return ShaderPreviewPublicationLinkValidationStatus::RegistryShapeMismatch;
			}
			const bool isPreviewBinding =
				previewEntry.m_ProgramRef == publication.m_ProgramRef &&
				previewEntry.m_TargetProfile == publication.m_TargetProfile;
			if (isPreviewBinding)
			{
				foundPreviewBinding = true;
				if (previewEntry.m_ArtifactRef != publication.m_ShaderArtifactRef)
				{
					return ShaderPreviewPublicationLinkValidationStatus::PreviewBindingMismatch;
				}
			}
			else if (previewEntry != baseEntry)
			{
				return ShaderPreviewPublicationLinkValidationStatus::NonPreviewBindingChanged;
			}
		}
		return foundPreviewBinding
			? ShaderPreviewPublicationLinkValidationStatus::Valid
			: ShaderPreviewPublicationLinkValidationStatus::PreviewBindingMismatch;
	}

	ShaderPreviewActivePublicationOrderingStatus
		ValidateShaderPreviewActivePublicationOrdering(
			const ShaderPreviewActivePublication* current,
			const ShaderPreviewActivePublication& candidate) noexcept
	{
		if (!IsValidShaderPreviewActivePublication(candidate))
		{
			return ShaderPreviewActivePublicationOrderingStatus::InvalidCandidate;
		}
		if (!current)
		{
			return ShaderPreviewActivePublicationOrderingStatus::Publishable;
		}
		if (!IsValidShaderPreviewActivePublication(*current))
		{
			return ShaderPreviewActivePublicationOrderingStatus::InvalidCurrent;
		}
		if (candidate.m_AttemptSequence <= current->m_AttemptSequence)
		{
			return ShaderPreviewActivePublicationOrderingStatus::NotNewer;
		}
		return ShaderPreviewActivePublicationOrderingStatus::Publishable;
	}

	bool IsKnownShaderPreviewRejectionCode(
		ShaderPreviewRejectionCode rejectionCode) noexcept
	{
		switch (rejectionCode)
		{
		case ShaderPreviewRejectionCode::None:
		case ShaderPreviewRejectionCode::PublicationUnavailable:
		case ShaderPreviewRejectionCode::PublicationInvalid:
		case ShaderPreviewRejectionCode::ShaderArtifactUnavailable:
		case ShaderPreviewRejectionCode::ShaderArtifactInvalid:
		case ShaderPreviewRejectionCode::RegistryUnavailable:
		case ShaderPreviewRejectionCode::RegistryInvalid:
		case ShaderPreviewRejectionCode::ActivationFailed:
		case ShaderPreviewRejectionCode::IOFailure:
			return true;
		}
		return false;
	}

	bool IsValidShaderPreviewObservation(
		const ShaderPreviewObservation& observation) noexcept
	{
		if (observation.m_SchemaVersion != ShaderPreviewObservationSchemaVersion ||
			!observation.m_ObservedPublicationRef.IsValid() ||
			!IsKnownShaderPreviewRejectionCode(observation.m_RejectionCode))
		{
			return false;
		}
		switch (observation.m_Status)
		{
		case ShaderPreviewObservationStatus::Loaded:
			return observation.m_RejectionCode == ShaderPreviewRejectionCode::None &&
				observation.m_LoadedPublicationRef ==
					observation.m_ObservedPublicationRef;
		case ShaderPreviewObservationStatus::Rejected:
			return observation.m_RejectionCode != ShaderPreviewRejectionCode::None &&
				observation.m_LoadedPublicationRef !=
					observation.m_ObservedPublicationRef;
		}
		return false;
	}

	ShaderPreviewObservationOrderingStatus ValidateShaderPreviewObservationOrdering(
		const ShaderPreviewObservation* current,
		const ShaderPreviewObservation& candidate) noexcept
	{
		if (!IsValidShaderPreviewObservation(candidate))
		{
			return ShaderPreviewObservationOrderingStatus::InvalidCandidate;
		}
		if (!current)
		{
			return ShaderPreviewObservationOrderingStatus::Publishable;
		}
		if (!IsValidShaderPreviewObservation(*current))
		{
			return ShaderPreviewObservationOrderingStatus::InvalidCurrent;
		}
		if (candidate.m_ObservedAttemptSequence <= current->m_ObservedAttemptSequence)
		{
			return ShaderPreviewObservationOrderingStatus::NotNewer;
		}
		if (candidate.m_Status == ShaderPreviewObservationStatus::Rejected &&
			candidate.m_LoadedPublicationRef != current->m_LoadedPublicationRef)
		{
			return ShaderPreviewObservationOrderingStatus::LastGoodChangedOnRejection;
		}
		return ShaderPreviewObservationOrderingStatus::Publishable;
	}
}
