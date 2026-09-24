#include "Artifact/ShaderRuntimeArtifactPublication.h"
#include "Artifact/PublicationTransaction.h"

#include "GGLabFoundation/Hash/Sha256.h"

#include <array>
#include <cstddef>
#include <filesystem>
#include <span>
#include <string>
#include <system_error>
#include <utility>

namespace gglab
{
	namespace
	{
		[[nodiscard]] ShaderArtifactCompatibilityRequest MakeCompatibilityRequest(
			const ShaderRuntimeArtifactManifest& manifest) noexcept
		{
			return {
				.m_TargetProfile = manifest.m_TargetProfile,
				.m_BinaryFormat = manifest.m_BinaryFormat,
				.m_SpirVTargetEnvironment = manifest.m_SpirVTargetEnvironment,
				.m_BindingABIRevision = manifest.m_BindingABIRevision,
				.m_CoordinateOptions = manifest.m_CoordinateOptions,
				.m_Stage = manifest.m_Stage,
			};
		}

		[[nodiscard]] bool IsValidPublicationInput(
			const ShaderRuntimeArtifact& artifact) noexcept
		{
			if (!artifact.m_Binary.IsValid() ||
				!ValidateShaderArtifactCompatibility(
					artifact.m_Manifest,
					MakeCompatibilityRequest(artifact.m_Manifest)).IsCompatible() ||
				artifact.m_Manifest.m_ArtifactId !=
					ComputeShaderArtifactId(artifact.m_Manifest))
			{
				return false;
			}

			const auto binaryBytes = std::span(
				static_cast<const std::byte*>(artifact.m_Binary.Data()),
				artifact.m_Binary.SizeInBytes());
			return ComputeSha256(binaryBytes) ==
					artifact.m_Manifest.m_BinaryContentDigest.m_Digest &&
				IsShaderBinaryFormat(
					artifact.m_Binary, artifact.m_Manifest.m_BinaryFormat);
		}

		[[nodiscard]] bool ObservePublishedArtifact(
			const ShaderLooseArtifactLocator& locator,
			const ShaderArtifactRef& artifactRef,
			const ShaderArtifactCompatibilityRequest& compatibility) noexcept
		{
			ShaderLooseArtifactReader reader(locator);
			ShaderArtifactStore store(reader);
			return store.LoadArtifact(artifactRef, compatibility).IsSuccess();
		}

		[[nodiscard]] bool ObservePublishedProgramRegistryArtifact(
			const ShaderLooseProgramRegistryArtifactLocator& locator,
			const ShaderProgramRegistryArtifactRef& registryRef) noexcept
		{
			ShaderLooseProgramRegistryArtifactReader reader(locator);
			return reader.ReadArtifact(registryRef).IsSuccess();
		}
	}

	ShaderRuntimeArtifact BuildShaderRuntimeArtifact(
		const ShaderArtifact& artifact)
	{
		ShaderRuntimeArtifact runtimeArtifact{};
		runtimeArtifact.m_Manifest = BuildShaderRuntimeArtifactManifest(artifact.m_Manifest);
		runtimeArtifact.m_Binary = artifact.m_Binary;
		return runtimeArtifact;
	}

	ShaderRuntimeArtifactPublicationResult PublishShaderRuntimeArtifact(
		const std::filesystem::path& artifactRoot,
		const ShaderArtifact& artifact) noexcept
	{
		ShaderRuntimeArtifactPublicationResult result{};
		try
		{
			ShaderRuntimeArtifact runtimeArtifact = BuildShaderRuntimeArtifact(artifact);
			if (artifactRoot.empty() || !IsValidPublicationInput(runtimeArtifact))
			{
				result.m_Status = ShaderRuntimeArtifactPublicationStatus::InvalidArtifact;
				return result;
			}

			result.m_ArtifactRef = {
				.m_ArtifactId = runtimeArtifact.m_Manifest.m_ArtifactId,
			};
			const ShaderLooseArtifactLocator locator(artifactRoot);
			result.m_Paths = locator.GetPaths(result.m_ArtifactRef);
			const ShaderArtifactCompatibilityRequest compatibility =
				MakeCompatibilityRequest(runtimeArtifact.m_Manifest);
			if (ObservePublishedArtifact(locator, result.m_ArtifactRef, compatibility))
			{
				result.m_Status = ShaderRuntimeArtifactPublicationStatus::AlreadyPresent;
				return result;
			}

			const SerializedShaderRuntimeArtifactManifest serializedManifest =
				SerializeShaderRuntimeArtifactManifest(runtimeArtifact.m_Manifest);
			const auto binaryBytes = std::span(
				static_cast<const std::byte*>(runtimeArtifact.m_Binary.Data()),
				runtimeArtifact.m_Binary.SizeInBytes());

			struct RuntimeArtifactObservation
			{
				const ShaderLooseArtifactLocator* m_Locator = nullptr;
				const ShaderArtifactRef* m_ArtifactRef = nullptr;
				const ShaderArtifactCompatibilityRequest* m_Compatibility = nullptr;
			};
			RuntimeArtifactObservation observation{
				.m_Locator = &locator,
				.m_ArtifactRef = &result.m_ArtifactRef,
				.m_Compatibility = &compatibility,
			};
			const std::array files{
				PublicationFileSpec{
					.m_Destination = result.m_Paths.m_BinaryPath,
					.m_Content = binaryBytes,
				},
				PublicationFileSpec{
					.m_Destination = result.m_Paths.m_ManifestPath,
					.m_Content = std::span<const std::byte>(serializedManifest),
				},
			};
			// Runtime artifacts are derived data: a candidate that remains
			// invalid after the observation window is repaired by removing
			// the commit record first, then its orphan/corrupt binary.
			const PublicationTransactionResult transaction = ExecutePublicationTransaction(
				files,
				{
					.m_Commit = PublicationCommit::ExclusiveRename,
					.m_ContinueWhenPrimaryPresent = true,
					.m_MaxAttempts = 2,
					.m_RemoveDestinationsOnFailedObservation = true,
					.m_ObservationAttempts = 64,
					.m_Observer = {
						.m_Context = &observation,
						.m_Observe = [](void* context) noexcept -> bool
						{
							auto* state = static_cast<RuntimeArtifactObservation*>(context);
							return ObservePublishedArtifact(
								*state->m_Locator, *state->m_ArtifactRef, *state->m_Compatibility);
						},
					},
				});
			if (!transaction.m_Observed)
			{
				return result;
			}
			result.m_Status = transaction.m_PrimaryCommitted && transaction.m_MarkerCommitted
				? ShaderRuntimeArtifactPublicationStatus::Published
				: ShaderRuntimeArtifactPublicationStatus::AlreadyPresent;
			return result;
		}
		catch (...)
		{
			return result;
		}
		return result;
	}

	ShaderProgramRegistryArtifactPublicationResult PublishShaderProgramRegistryArtifact(
		const std::filesystem::path& artifactRoot,
		const ShaderProgramRegistryArtifact& artifact) noexcept
	{
		ShaderProgramRegistryArtifactPublicationResult result{};
		try
		{
			if (artifactRoot.empty() ||
				ValidateShaderProgramRegistryArtifact(artifact) !=
					ShaderProgramRegistryArtifactValidationStatus::Valid)
			{
				result.m_Status =
					ShaderProgramRegistryArtifactPublicationStatus::InvalidArtifact;
				return result;
			}

			result.m_RegistryRef = { .m_RegistryId = artifact.m_RegistryId };
			const ShaderLooseProgramRegistryArtifactLocator locator(artifactRoot);
			result.m_Path = locator.GetPath(result.m_RegistryRef);
			if (ObservePublishedProgramRegistryArtifact(locator, result.m_RegistryRef))
			{
				result.m_Status =
					ShaderProgramRegistryArtifactPublicationStatus::AlreadyPresent;
				return result;
			}

			const SerializedShaderProgramRegistryArtifact serializedArtifact =
				SerializeShaderProgramRegistryArtifact(artifact);
			if (serializedArtifact.empty())
			{
				result.m_Status =
					ShaderProgramRegistryArtifactPublicationStatus::InvalidArtifact;
				return result;
			}

			struct ProgramRegistryObservation
			{
				const ShaderLooseProgramRegistryArtifactLocator* m_Locator = nullptr;
				const ShaderProgramRegistryArtifactRef* m_RegistryRef = nullptr;
			};
			ProgramRegistryObservation observation{
				.m_Locator = &locator,
				.m_RegistryRef = &result.m_RegistryRef,
			};
			const std::array files{
				PublicationFileSpec{
					.m_Destination = result.m_Path.m_Path,
					.m_Content = std::span<const std::byte>(serializedArtifact),
				},
			};
			// Content-addressed registry snapshots are immutable. Repair of a
			// persistently corrupt destination is bounded to the retry loop;
			// active-registry and arbitrary multi-writer policy belong to the
			// development handoff contract.
			const PublicationTransactionResult transaction = ExecutePublicationTransaction(
				files,
				{
					.m_Commit = PublicationCommit::ExclusiveRename,
					.m_MaxAttempts = 2,
					.m_RemoveDestinationsOnFailedObservation = true,
					.m_ObservationAttempts = 64,
					.m_Observer = {
						.m_Context = &observation,
						.m_Observe = [](void* context) noexcept -> bool
						{
							auto* state = static_cast<ProgramRegistryObservation*>(context);
							return ObservePublishedProgramRegistryArtifact(
								*state->m_Locator, *state->m_RegistryRef);
						},
					},
				});
			if (!transaction.m_Observed)
			{
				return result;
			}
			result.m_Status = transaction.m_PrimaryCommitted
				? ShaderProgramRegistryArtifactPublicationStatus::Published
				: ShaderProgramRegistryArtifactPublicationStatus::AlreadyPresent;
			return result;
		}
		catch (...)
		{
			return result;
		}
		return result;
	}

	ActiveShaderProgramRegistryPublicationResult PublishActiveShaderProgramRegistry(
		const std::filesystem::path& artifactRoot,
		ShaderTargetProfile targetProfile,
		const ShaderProgramRegistryArtifactRef& registryRef) noexcept
	{
		ActiveShaderProgramRegistryPublicationResult result{
			.m_RegistryRef = registryRef,
		};
		try
		{
			if (artifactRoot.empty() || !artifactRoot.is_absolute() ||
				!IsKnownShaderTargetProfile(targetProfile) || !registryRef.IsValid())
			{
				result.m_Status =
					ActiveShaderProgramRegistryPublicationStatus::InvalidRegistry;
				return result;
			}

			ShaderLooseProgramRegistryArtifactReader registryReader{
				ShaderLooseProgramRegistryArtifactLocator(artifactRoot)
			};
			if (!registryReader.ReadArtifact(registryRef).IsSuccess())
			{
				result.m_Status =
					ActiveShaderProgramRegistryPublicationStatus::InvalidRegistry;
				return result;
			}

			const ShaderLooseActiveProgramRegistryLocator locator(artifactRoot, targetProfile);
			result.m_Path = locator.GetPath();
			ShaderLooseActiveProgramRegistryReader reader(locator);
			const ActiveShaderProgramRegistryReadResult current = reader.Read();
			if (current.IsSuccess() && current.m_RegistryRef == registryRef)
			{
				result.m_Status = ActiveShaderProgramRegistryPublicationStatus::AlreadyActive;
				return result;
			}

			const SerializedActiveShaderProgramRegistry serialized =
				SerializeActiveShaderProgramRegistry(registryRef);
			const std::array files{
				PublicationFileSpec{
					.m_Destination = result.m_Path,
					.m_Content = std::span<const std::byte>(serialized),
				},
			};
			const PublicationTransactionResult transaction = ExecutePublicationTransaction(
				files, { .m_Commit = PublicationCommit::ReplaceExisting });
			if (!transaction.m_PrimaryCommitted)
			{
				return result;
			}
			result.m_Status = ActiveShaderProgramRegistryPublicationStatus::Published;
		}
		catch (...)
		{
			return result;
		}
		return result;
	}


}
