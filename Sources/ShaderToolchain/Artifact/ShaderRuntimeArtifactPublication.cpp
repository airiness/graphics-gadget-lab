#include "Artifact/ShaderRuntimeArtifactPublication.h"

#include "GGLabFoundation/Hash/Sha256.h"
#include "GGLabFoundation/IO/PathUtils.h"

#include <process.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <span>
#include <string>
#include <system_error>
#include <thread>
#include <vector>

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

		[[nodiscard]] std::filesystem::path MakeUniqueTempPath(
			const std::filesystem::path& destination)
		{
			static std::atomic_uint64_t counter = 0;
			return destination.wstring() + L".tmp." +
				std::to_wstring(static_cast<uint32_t>(::_getpid())) + L"." +
				std::to_wstring(counter.fetch_add(1, std::memory_order_relaxed));
		}

		void RemoveFileBestEffort(const std::filesystem::path& path) noexcept
		{
			std::error_code ignored;
			std::filesystem::remove(path, ignored);
		}

		[[nodiscard]] bool PublishFile(
			const std::filesystem::path& source,
			const std::filesystem::path& destination) noexcept
		{
			std::error_code errorCode;
			std::filesystem::rename(source, destination, errorCode);
			return !errorCode;
		}

		[[nodiscard]] bool HasFile(const std::filesystem::path& path) noexcept
		{
			std::error_code errorCode;
			return std::filesystem::is_regular_file(path, errorCode);
		}

		[[nodiscard]] bool FileEquals(
			const std::filesystem::path& path,
			std::span<const std::byte> expectedBytes)
		{
			std::ifstream input(path, std::ios::binary);
			if (!input)
			{
				return false;
			}
			std::vector<std::byte> observed(expectedBytes.size());
			input.read(
				reinterpret_cast<char*>(observed.data()),
				static_cast<std::streamsize>(observed.size()));
			return input.gcount() == static_cast<std::streamsize>(observed.size()) &&
				input.peek() == std::char_traits<char>::eof() &&
				std::ranges::equal(observed, expectedBytes);
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

			if (!utils::CreateParentDirectoryIfNotExist(result.m_Paths.m_BinaryPath) ||
				!utils::CreateParentDirectoryIfNotExist(result.m_Paths.m_ManifestPath))
			{
				return result;
			}

			const SerializedShaderRuntimeArtifactManifest serializedManifest =
				SerializeShaderRuntimeArtifactManifest(runtimeArtifact.m_Manifest);
			const auto binaryBytes = std::span(
				static_cast<const std::byte*>(runtimeArtifact.m_Binary.Data()),
				runtimeArtifact.m_Binary.SizeInBytes());

			constexpr int MaxPublishAttempts = 2;
			for (int publishAttempt = 0; publishAttempt < MaxPublishAttempts; ++publishAttempt)
			{
				const std::filesystem::path tempBinaryPath =
					MakeUniqueTempPath(result.m_Paths.m_BinaryPath);
				const std::filesystem::path tempManifestPath =
					MakeUniqueTempPath(result.m_Paths.m_ManifestPath);
				const bool tempsWritten =
					utils::WriteFileBinary(tempBinaryPath, binaryBytes) &&
					utils::WriteFileBinary(tempManifestPath, serializedManifest);
				if (!tempsWritten)
				{
					RemoveFileBestEffort(tempBinaryPath);
					RemoveFileBestEffort(tempManifestPath);
					return result;
				}
				if (!FileEquals(tempBinaryPath, binaryBytes) ||
					!FileEquals(tempManifestPath, serializedManifest))
				{
					RemoveFileBestEffort(tempBinaryPath);
					RemoveFileBestEffort(tempManifestPath);
					return result;
				}

				const bool binaryPublished =
					PublishFile(tempBinaryPath, result.m_Paths.m_BinaryPath);
				bool manifestPublished = false;
				if (binaryPublished || HasFile(result.m_Paths.m_BinaryPath))
				{
					manifestPublished =
						PublishFile(tempManifestPath, result.m_Paths.m_ManifestPath);
				}
				RemoveFileBestEffort(tempBinaryPath);
				RemoveFileBestEffort(tempManifestPath);

				constexpr int MaxObservationAttempts = 64;
				for (int observationAttempt = 0;
					observationAttempt < MaxObservationAttempts;
					++observationAttempt)
				{
					if (ObservePublishedArtifact(
						locator, result.m_ArtifactRef, compatibility))
					{
						result.m_Status = binaryPublished && manifestPublished
							? ShaderRuntimeArtifactPublicationStatus::Published
							: ShaderRuntimeArtifactPublicationStatus::AlreadyPresent;
						return result;
					}
					std::this_thread::sleep_for(std::chrono::milliseconds(1));
				}

				// Runtime artifacts are derived data. A candidate that remains
				// invalid after the observation window is recoverable: remove the
				// commit record first, then its orphan/corrupt binary, and retry.
				RemoveFileBestEffort(result.m_Paths.m_ManifestPath);
				RemoveFileBestEffort(result.m_Paths.m_BinaryPath);
			}
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

			if (!utils::CreateParentDirectoryIfNotExist(result.m_Path.m_Path))
			{
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

			constexpr int MaxPublishAttempts = 2;
			for (int publishAttempt = 0; publishAttempt < MaxPublishAttempts; ++publishAttempt)
			{
				const std::filesystem::path tempPath =
					MakeUniqueTempPath(result.m_Path.m_Path);
				if (!utils::WriteFileBinary(tempPath, serializedArtifact) ||
					!FileEquals(tempPath, serializedArtifact))
				{
					RemoveFileBestEffort(tempPath);
					return result;
				}

				const bool published = PublishFile(tempPath, result.m_Path.m_Path);
				RemoveFileBestEffort(tempPath);
				constexpr int MaxObservationAttempts = 64;
				for (int observationAttempt = 0;
					observationAttempt < MaxObservationAttempts;
					++observationAttempt)
				{
					if (ObservePublishedProgramRegistryArtifact(
						locator, result.m_RegistryRef))
					{
						result.m_Status = published
							? ShaderProgramRegistryArtifactPublicationStatus::Published
							: ShaderProgramRegistryArtifactPublicationStatus::AlreadyPresent;
						return result;
					}
					std::this_thread::sleep_for(std::chrono::milliseconds(1));
				}

				// R5 snapshots are immutable. Repair of a persistently corrupt slot is
				// bounded here; active-snapshot and arbitrary multi-writer policy belong
				// to the development handoff contract.
				RemoveFileBestEffort(result.m_Path.m_Path);
			}
		}
		catch (...)
		{
			return result;
		}
		return result;
	}
}
