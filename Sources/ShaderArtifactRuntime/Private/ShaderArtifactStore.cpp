#include "ShaderArtifactRuntime/ShaderArtifactStore.h"
#include "ShaderArtifactRuntime/ShaderArtifact.h"

#include "GGLabFoundation/Hash/Sha256.h"

#include <cstddef>
#include <span>
#include <utility>

namespace gglab
{
	namespace
	{
		[[nodiscard]] constexpr bool IsKnownStage(ShaderStage stage) noexcept
		{
			switch (stage)
			{
			case ShaderStage::Vertex:
			case ShaderStage::Pixel:
			case ShaderStage::Hull:
			case ShaderStage::Domain:
			case ShaderStage::Geometry:
			case ShaderStage::Mesh:
			case ShaderStage::Compute:
				return true;
			}
			return false;
		}

		[[nodiscard]] constexpr bool HasKnownCoordinateOptions(
			ShaderCoordinateOptions options) noexcept
		{
			constexpr uint8_t KnownOptions =
				static_cast<uint8_t>(ShaderCoordinateOptions::InvertY) |
				static_cast<uint8_t>(ShaderCoordinateOptions::UseDxPositionW);
			return (static_cast<uint8_t>(options) & ~KnownOptions) == 0;
		}

		[[nodiscard]] constexpr bool HasKnownTargetVocabulary(
			ShaderTargetProfile targetProfile,
			ShaderBinaryFormat binaryFormat,
			ShaderSpirVTargetEnvironment environment,
			ShaderCoordinateOptions coordinateOptions,
			ShaderStage stage) noexcept
		{
			const bool knownProfile =
				targetProfile == ShaderTargetProfile::GGLabDX12 ||
				targetProfile == ShaderTargetProfile::GGLabVulkan13;
			const bool knownFormat =
				binaryFormat == ShaderBinaryFormat::Dxil ||
				binaryFormat == ShaderBinaryFormat::SpirV;
			const bool knownEnvironment =
				environment == ShaderSpirVTargetEnvironment::None ||
				environment == ShaderSpirVTargetEnvironment::Vulkan1_3;
			return knownProfile && knownFormat && knownEnvironment &&
				HasKnownCoordinateOptions(coordinateOptions) && IsKnownStage(stage);
		}

		[[nodiscard]] constexpr bool IsValidTargetContract(
			ShaderTargetProfile targetProfile,
			ShaderBinaryFormat binaryFormat,
			ShaderSpirVTargetEnvironment environment,
			uint32_t bindingABIRevision,
			ShaderCoordinateOptions coordinateOptions,
			ShaderStage stage) noexcept
		{
			if (!IsKnownStage(stage) || !HasKnownCoordinateOptions(coordinateOptions))
			{
				return false;
			}

			switch (targetProfile)
			{
			case ShaderTargetProfile::GGLabDX12:
				return binaryFormat == ShaderBinaryFormat::Dxil &&
					environment == ShaderSpirVTargetEnvironment::None &&
					bindingABIRevision == 0 &&
					coordinateOptions == ShaderCoordinateOptions::None;
			case ShaderTargetProfile::GGLabVulkan13:
				return binaryFormat == ShaderBinaryFormat::SpirV &&
					environment == ShaderSpirVTargetEnvironment::Vulkan1_3 &&
					bindingABIRevision != 0;
			}
			return false;
		}
	}

	ShaderArtifactCompatibilityResult ValidateShaderArtifactCompatibility(
		const ShaderRuntimeArtifactManifest& manifest,
		const ShaderArtifactCompatibilityRequest& request) noexcept
	{
		if (!IsValidTargetContract(
			request.m_TargetProfile,
			request.m_BinaryFormat,
			request.m_SpirVTargetEnvironment,
			request.m_BindingABIRevision,
			request.m_CoordinateOptions,
			request.m_Stage))
		{
			return { .m_Status = ShaderArtifactCompatibilityStatus::InvalidRequest };
		}
		if (manifest.m_SchemaVersion != ShaderRuntimeArtifactManifestSchemaVersion)
		{
			return { .m_Status =
				ShaderArtifactCompatibilityStatus::UnsupportedManifestSchema };
		}
		if (!HasKnownTargetVocabulary(
			manifest.m_TargetProfile,
			manifest.m_BinaryFormat,
			manifest.m_SpirVTargetEnvironment,
			manifest.m_CoordinateOptions,
			manifest.m_Stage))
		{
			return { .m_Status = ShaderArtifactCompatibilityStatus::InvalidManifestTarget };
		}
		if (manifest.m_TargetProfile != request.m_TargetProfile)
		{
			return { .m_Status = ShaderArtifactCompatibilityStatus::TargetProfileMismatch };
		}
		if (manifest.m_BinaryFormat != request.m_BinaryFormat)
		{
			return { .m_Status = ShaderArtifactCompatibilityStatus::BinaryFormatMismatch };
		}
		if (manifest.m_SpirVTargetEnvironment != request.m_SpirVTargetEnvironment)
		{
			return { .m_Status =
				ShaderArtifactCompatibilityStatus::TargetEnvironmentMismatch };
		}
		if (manifest.m_BindingABIRevision != request.m_BindingABIRevision)
		{
			return { .m_Status =
				ShaderArtifactCompatibilityStatus::BindingABIRevisionMismatch };
		}
		if (manifest.m_CoordinateOptions != request.m_CoordinateOptions)
		{
			return { .m_Status =
				ShaderArtifactCompatibilityStatus::CoordinateOptionsMismatch };
		}
		if (manifest.m_Stage != request.m_Stage)
		{
			return { .m_Status = ShaderArtifactCompatibilityStatus::ShaderStageMismatch };
		}
		return { .m_Status = ShaderArtifactCompatibilityStatus::Compatible };
	}

	ShaderArtifactStore::ShaderArtifactStore(ShaderArtifactReaderBase& reader) noexcept
		: m_Reader(reader)
	{
	}

	ShaderArtifactLoadResult ShaderArtifactStore::LoadArtifact(
		const ShaderArtifactRef& artifactRef,
		const ShaderArtifactCompatibilityRequest& compatibility) noexcept
	{
		if (!artifactRef.IsValid())
		{
			return { .m_Status = ShaderArtifactLoadStatus::InvalidReference };
		}
		if (!IsValidTargetContract(
			compatibility.m_TargetProfile,
			compatibility.m_BinaryFormat,
			compatibility.m_SpirVTargetEnvironment,
			compatibility.m_BindingABIRevision,
			compatibility.m_CoordinateOptions,
			compatibility.m_Stage))
		{
			return {
				.m_Status = ShaderArtifactLoadStatus::InvalidCompatibilityRequest,
				.m_Compatibility = {
					.m_Status = ShaderArtifactCompatibilityStatus::InvalidRequest,
				},
			};
		}

		ShaderArtifactReadResult readResult = m_Reader.ReadArtifact(artifactRef);
		switch (readResult.m_Status)
		{
		case ShaderArtifactReadStatus::NotFound:
			return { .m_Status = ShaderArtifactLoadStatus::NotFound };
		case ShaderArtifactReadStatus::IOFailure:
			return { .m_Status = ShaderArtifactLoadStatus::ReadFailure };
		case ShaderArtifactReadStatus::MalformedArtifact:
			return { .m_Status = ShaderArtifactLoadStatus::MalformedArtifact };
		case ShaderArtifactReadStatus::Success:
			break;
		default:
			return { .m_Status = ShaderArtifactLoadStatus::ReadFailure };
		}

		ShaderRuntimeArtifact& artifact = readResult.m_Artifact;
		const ShaderArtifactCompatibilityResult compatibilityResult =
			ValidateShaderArtifactCompatibility(artifact.m_Manifest, compatibility);
		if (!compatibilityResult.IsCompatible())
		{
			return {
				.m_Status = ShaderArtifactLoadStatus::IncompatibleArtifact,
				.m_Compatibility = compatibilityResult,
			};
		}

		const ShaderArtifactId derivedArtifactId =
			ComputeShaderArtifactId(artifact.m_Manifest);
		if (!artifact.m_Manifest.m_ArtifactId.IsValid() ||
			artifact.m_Manifest.m_ArtifactId != artifactRef.m_ArtifactId ||
			derivedArtifactId != artifactRef.m_ArtifactId)
		{
			return {
				.m_Status = ShaderArtifactLoadStatus::ArtifactIdentityMismatch,
				.m_Compatibility = compatibilityResult,
			};
		}

		if (!artifact.m_Binary.IsValid())
		{
			return {
				.m_Status = ShaderArtifactLoadStatus::InvalidBinary,
				.m_Compatibility = compatibilityResult,
			};
		}
		const auto binaryBytes = std::span(
			static_cast<const std::byte*>(artifact.m_Binary.Data()),
			artifact.m_Binary.SizeInBytes());
		if (ComputeSha256(binaryBytes) !=
			artifact.m_Manifest.m_BinaryContentDigest.m_Digest)
		{
			return {
				.m_Status = ShaderArtifactLoadStatus::BinaryDigestMismatch,
				.m_Compatibility = compatibilityResult,
			};
		}
		if (!IsShaderBinaryFormat(
			artifact.m_Binary, artifact.m_Manifest.m_BinaryFormat))
		{
			return {
				.m_Status = ShaderArtifactLoadStatus::InvalidBinary,
				.m_Compatibility = compatibilityResult,
			};
		}

		return {
			.m_Status = ShaderArtifactLoadStatus::Success,
			.m_Compatibility = compatibilityResult,
			.m_Artifact = std::move(artifact),
		};
	}
}
