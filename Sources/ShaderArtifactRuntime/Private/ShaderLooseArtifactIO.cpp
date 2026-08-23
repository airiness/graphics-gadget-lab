#include "ShaderArtifactRuntime/ShaderLooseArtifactIO.h"

#include "GGLabFoundation/Hash/Sha256.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <limits>
#include <string>
#include <system_error>
#include <utility>

namespace gglab
{
	namespace
	{
		constexpr std::array<std::byte, 8> ManifestMagic{
			std::byte{ 'G' },
			std::byte{ 'G' },
			std::byte{ 'S' },
			std::byte{ 'H' },
			std::byte{ 'A' },
			std::byte{ 'D' },
			std::byte{ 'R' },
			std::byte{ 'T' },
		};

		void WriteU32LE(
			SerializedShaderRuntimeArtifactManifest& bytes,
			size_t& offset,
			uint32_t value) noexcept
		{
			for (size_t byteIndex = 0; byteIndex < sizeof(value); ++byteIndex)
			{
				bytes[offset++] = static_cast<std::byte>(value & 0xFFu);
				value >>= 8u;
			}
		}

		[[nodiscard]] uint32_t ReadU32LE(
			std::span<const std::byte> bytes, size_t& offset) noexcept
		{
			uint32_t value = 0;
			for (size_t byteIndex = 0; byteIndex < sizeof(value); ++byteIndex)
			{
				value |= std::to_integer<uint32_t>(bytes[offset++]) << (byteIndex * 8u);
			}
			return value;
		}

		enum class FileReadStatus : uint8_t
		{
			Success,
			Missing,
			Malformed,
			Failure,
		};

		[[nodiscard]] FileReadStatus ReadFile(
			const std::filesystem::path& path,
			uintmax_t maximumSize,
			ShaderBinary& outBinary) noexcept
		{
			std::error_code errorCode;
			const std::filesystem::file_status status =
				std::filesystem::status(path, errorCode);
			if (errorCode)
			{
				if (errorCode == std::errc::no_such_file_or_directory)
				{
					return FileReadStatus::Missing;
				}
				return FileReadStatus::Failure;
			}
			if (!std::filesystem::exists(status))
			{
				return FileReadStatus::Missing;
			}
			if (!std::filesystem::is_regular_file(status))
			{
				return FileReadStatus::Failure;
			}

			const uintmax_t fileSize = std::filesystem::file_size(path, errorCode);
			if (errorCode)
			{
				return FileReadStatus::Failure;
			}
			if (fileSize == 0 || fileSize > maximumSize ||
				fileSize > static_cast<uintmax_t>((std::numeric_limits<size_t>::max)()))
			{
				return FileReadStatus::Malformed;
			}

			std::ifstream input(path, std::ios::binary);
			if (!input)
			{
				return FileReadStatus::Failure;
			}
			ShaderBinary binary(static_cast<size_t>(fileSize));
			input.read(static_cast<char*>(binary.Data()), static_cast<std::streamsize>(fileSize));
			if (!input || input.gcount() != static_cast<std::streamsize>(fileSize))
			{
				return FileReadStatus::Failure;
			}
			outBinary = std::move(binary);
			return FileReadStatus::Success;
		}
	}

	SerializedShaderRuntimeArtifactManifest SerializeShaderRuntimeArtifactManifest(
		const ShaderRuntimeArtifactManifest& manifest) noexcept
	{
		SerializedShaderRuntimeArtifactManifest bytes{};
		size_t offset = 0;
		std::ranges::copy(ManifestMagic, bytes.begin());
		offset += ManifestMagic.size();
		WriteU32LE(bytes, offset, ShaderRuntimeArtifactFileFormatVersion);
		WriteU32LE(bytes, offset, manifest.m_SchemaVersion);
		std::ranges::copy(manifest.m_ArtifactId.m_DurableDigest.m_Value, bytes.begin() + offset);
		offset += Sha256Digest::Size;
		bytes[offset++] = static_cast<std::byte>(manifest.m_TargetProfile);
		bytes[offset++] = static_cast<std::byte>(manifest.m_BinaryFormat);
		bytes[offset++] = static_cast<std::byte>(manifest.m_SpirVTargetEnvironment);
		bytes[offset++] = static_cast<std::byte>(manifest.m_CoordinateOptions);
		WriteU32LE(bytes, offset, manifest.m_BindingABIRevision);
		WriteU32LE(bytes, offset, static_cast<uint32_t>(manifest.m_Stage));
		std::ranges::copy(
			manifest.m_BinaryContentDigest.m_Digest.m_Value, bytes.begin() + offset);
		return bytes;
	}

	std::optional<ShaderRuntimeArtifactManifest> DeserializeShaderRuntimeArtifactManifest(
		std::span<const std::byte> bytes) noexcept
	{
		if (bytes.size() != SerializedShaderRuntimeArtifactManifestSize ||
			!std::ranges::equal(ManifestMagic, bytes.first(ManifestMagic.size())))
		{
			return std::nullopt;
		}

		size_t offset = ManifestMagic.size();
		if (ReadU32LE(bytes, offset) != ShaderRuntimeArtifactFileFormatVersion)
		{
			return std::nullopt;
		}

		ShaderRuntimeArtifactManifest manifest{};
		manifest.m_SchemaVersion = ReadU32LE(bytes, offset);
		std::ranges::copy_n(
			bytes.begin() + offset,
			Sha256Digest::Size,
			manifest.m_ArtifactId.m_DurableDigest.m_Value.begin());
		offset += Sha256Digest::Size;
		manifest.m_TargetProfile =
			static_cast<ShaderTargetProfile>(std::to_integer<uint8_t>(bytes[offset++]));
		manifest.m_BinaryFormat =
			static_cast<ShaderBinaryFormat>(std::to_integer<uint8_t>(bytes[offset++]));
		manifest.m_SpirVTargetEnvironment = static_cast<ShaderSpirVTargetEnvironment>(
			std::to_integer<uint8_t>(bytes[offset++]));
		manifest.m_CoordinateOptions = static_cast<ShaderCoordinateOptions>(
			std::to_integer<uint8_t>(bytes[offset++]));
		manifest.m_BindingABIRevision = ReadU32LE(bytes, offset);
		manifest.m_Stage = static_cast<ShaderStage>(ReadU32LE(bytes, offset));
		std::ranges::copy_n(
			bytes.begin() + offset,
			Sha256Digest::Size,
			manifest.m_BinaryContentDigest.m_Digest.m_Value.begin());
		return manifest;
	}

	ShaderLooseArtifactLocator::ShaderLooseArtifactLocator(std::filesystem::path root)
		: m_Root(std::move(root))
	{
	}

	const std::filesystem::path& ShaderLooseArtifactLocator::GetRoot() const noexcept
	{
		return m_Root;
	}

	ShaderLooseArtifactPaths ShaderLooseArtifactLocator::GetPaths(
		const ShaderArtifactRef& artifactRef) const
	{
		if (!artifactRef.IsValid())
		{
			return {};
		}
		const std::string artifactId =
			Sha256DigestToHex(artifactRef.m_ArtifactId.m_DurableDigest);
		const std::filesystem::path directory =
			m_Root / artifactId.substr(0, 2) / artifactId.substr(2, 2);
		const std::filesystem::path baseName = artifactId + ".ggsh";
		return {
			.m_BinaryPath = directory / (baseName.string() + ".bin"),
			.m_ManifestPath = directory / (baseName.string() + ".manifest"),
		};
	}

	ShaderLooseArtifactReader::ShaderLooseArtifactReader(ShaderLooseArtifactLocator locator)
		: m_Locator(std::move(locator))
	{
	}

	const ShaderLooseArtifactLocator& ShaderLooseArtifactReader::GetLocator() const noexcept
	{
		return m_Locator;
	}

	ShaderArtifactReadResult ShaderLooseArtifactReader::ReadArtifact(
		const ShaderArtifactRef& artifactRef) noexcept
	{
		if (!artifactRef.IsValid() || m_Locator.GetRoot().empty())
		{
			return { .m_Status = ShaderArtifactReadStatus::MalformedArtifact };
		}

		try
		{
			const ShaderLooseArtifactPaths paths = m_Locator.GetPaths(artifactRef);
			ShaderBinary serializedManifest;
			const FileReadStatus manifestRead = ReadFile(
				paths.m_ManifestPath,
				SerializedShaderRuntimeArtifactManifestSize,
				serializedManifest);
			if (manifestRead == FileReadStatus::Missing)
			{
				return { .m_Status = ShaderArtifactReadStatus::NotFound };
			}
			if (manifestRead == FileReadStatus::Failure)
			{
				return { .m_Status = ShaderArtifactReadStatus::IOFailure };
			}
			if (manifestRead == FileReadStatus::Malformed ||
				serializedManifest.SizeInBytes() !=
					SerializedShaderRuntimeArtifactManifestSize)
			{
				return { .m_Status = ShaderArtifactReadStatus::MalformedArtifact };
			}

			const auto manifestBytes = std::span(
				static_cast<const std::byte*>(serializedManifest.Data()),
				serializedManifest.SizeInBytes());
			const std::optional<ShaderRuntimeArtifactManifest> manifest =
				DeserializeShaderRuntimeArtifactManifest(manifestBytes);
			if (!manifest.has_value())
			{
				return { .m_Status = ShaderArtifactReadStatus::MalformedArtifact };
			}

			ShaderBinary binary;
			const FileReadStatus binaryRead = ReadFile(
				paths.m_BinaryPath, MaxLooseShaderArtifactBinarySize, binary);
			if (binaryRead == FileReadStatus::Missing)
			{
				return { .m_Status = ShaderArtifactReadStatus::MalformedArtifact };
			}
			if (binaryRead == FileReadStatus::Malformed)
			{
				return { .m_Status = ShaderArtifactReadStatus::MalformedArtifact };
			}
			if (binaryRead != FileReadStatus::Success)
			{
				return { .m_Status = ShaderArtifactReadStatus::IOFailure };
			}

			return {
				.m_Status = ShaderArtifactReadStatus::Success,
				.m_Artifact = {
					.m_Manifest = *manifest,
					.m_Binary = std::move(binary),
				},
			};
		}
		catch (...)
		{
			return { .m_Status = ShaderArtifactReadStatus::IOFailure };
		}
	}
}
