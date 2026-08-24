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
		constexpr std::array<std::byte, 8> ProgramRegistryMagic{
			std::byte{ 'G' },
			std::byte{ 'G' },
			std::byte{ 'S' },
			std::byte{ 'H' },
			std::byte{ 'R' },
			std::byte{ 'E' },
			std::byte{ 'G' },
			std::byte{ 0 },
		};
		constexpr std::array<std::byte, 8> ActiveProgramRegistryMagic{
			std::byte{ 'G' },
			std::byte{ 'G' },
			std::byte{ 'S' },
			std::byte{ 'H' },
			std::byte{ 'L' },
			std::byte{ 'I' },
			std::byte{ 'V' },
			std::byte{ 'E' },
		};

		template<class Container>
		void WriteU32LE(
			Container& bytes,
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

		[[nodiscard]] bool TryReadU32LE(
			std::span<const std::byte> bytes,
			size_t& offset,
			uint32_t& outValue) noexcept
		{
			if (offset > bytes.size() || bytes.size() - offset < sizeof(uint32_t))
			{
				return false;
			}
			outValue = ReadU32LE(bytes, offset);
			return true;
		}

		[[nodiscard]] bool CanRead(
			std::span<const std::byte> bytes, size_t offset, size_t count) noexcept
		{
			return offset <= bytes.size() && count <= bytes.size() - offset;
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
		if (!IsValidShaderRuntimeEntryPoint(manifest.m_EntryPoint))
		{
			return {};
		}

		try
		{
			SerializedShaderRuntimeArtifactManifest bytes(
				SerializedShaderRuntimeArtifactManifestFixedSize +
				manifest.m_EntryPoint.size());
			size_t offset = 0;
			std::ranges::copy(ManifestMagic, bytes.begin());
			offset += ManifestMagic.size();
			WriteU32LE(bytes, offset, ShaderRuntimeArtifactFileFormatVersion);
			WriteU32LE(bytes, offset, manifest.m_SchemaVersion);
			std::ranges::copy(
				manifest.m_ArtifactId.m_DurableDigest.m_Value, bytes.begin() + offset);
			offset += Sha256Digest::Size;
			bytes[offset++] = static_cast<std::byte>(manifest.m_TargetProfile);
			bytes[offset++] = static_cast<std::byte>(manifest.m_BinaryFormat);
			bytes[offset++] = static_cast<std::byte>(manifest.m_SpirVTargetEnvironment);
			bytes[offset++] = static_cast<std::byte>(manifest.m_CoordinateOptions);
			WriteU32LE(bytes, offset, manifest.m_BindingABIRevision);
			WriteU32LE(bytes, offset, static_cast<uint32_t>(manifest.m_Stage));
			WriteU32LE(bytes, offset, static_cast<uint32_t>(manifest.m_EntryPoint.size()));
			for (const char character : manifest.m_EntryPoint)
			{
				bytes[offset++] = static_cast<std::byte>(character);
			}
			std::ranges::copy(
				manifest.m_BinaryContentDigest.m_Digest.m_Value, bytes.begin() + offset);
			return bytes;
		}
		catch (...)
		{
			return {};
		}
	}

	std::optional<ShaderRuntimeArtifactManifest> DeserializeShaderRuntimeArtifactManifest(
		std::span<const std::byte> bytes) noexcept
	{
		if (bytes.size() < SerializedShaderRuntimeArtifactManifestFixedSize ||
			bytes.size() > MaxSerializedShaderRuntimeArtifactManifestSize ||
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
		const uint32_t entryPointSize = ReadU32LE(bytes, offset);
		if (entryPointSize == 0 || entryPointSize > MaxShaderRuntimeEntryPointSize ||
			bytes.size() != SerializedShaderRuntimeArtifactManifestFixedSize +
				entryPointSize)
		{
			return std::nullopt;
		}
		try
		{
			manifest.m_EntryPoint.assign(
				reinterpret_cast<const char*>(bytes.data() + offset), entryPointSize);
		}
		catch (...)
		{
			return std::nullopt;
		}
		if (!IsValidShaderRuntimeEntryPoint(manifest.m_EntryPoint))
		{
			return std::nullopt;
		}
		offset += entryPointSize;
		std::ranges::copy_n(
			bytes.begin() + offset,
			Sha256Digest::Size,
			manifest.m_BinaryContentDigest.m_Digest.m_Value.begin());
		return manifest;
	}

	SerializedShaderProgramRegistryArtifact SerializeShaderProgramRegistryArtifact(
		const ShaderProgramRegistryArtifact& artifact) noexcept
	{
		if (ValidateShaderProgramRegistryArtifact(artifact) !=
			ShaderProgramRegistryArtifactValidationStatus::Valid)
		{
			return {};
		}

		try
		{
			size_t serializedSize = SerializedShaderProgramRegistryArtifactHeaderSize;
			for (const ShaderProgramRegistryEntry& entry : artifact.m_Entries)
			{
				serializedSize += SerializedShaderProgramRegistryEntryFixedSize +
					entry.m_ProgramRef.m_ProgramId.size() +
					entry.m_ProgramRef.m_VariantId.size();
			}
			SerializedShaderProgramRegistryArtifact bytes(serializedSize);
			size_t offset = 0;
			std::ranges::copy(ProgramRegistryMagic, bytes.begin());
			offset += ProgramRegistryMagic.size();
			WriteU32LE(bytes, offset, ShaderProgramRegistryArtifactFileFormatVersion);
			WriteU32LE(bytes, offset, artifact.m_SchemaVersion);
			std::ranges::copy(
				artifact.m_RegistryId.m_DurableDigest.m_Value, bytes.begin() + offset);
			offset += Sha256Digest::Size;
			WriteU32LE(bytes, offset, static_cast<uint32_t>(artifact.m_Entries.size()));

			for (const ShaderProgramRegistryEntry& entry : artifact.m_Entries)
			{
				WriteU32LE(bytes, offset,
					static_cast<uint32_t>(entry.m_ProgramRef.m_ProgramId.size()));
				for (const char character : entry.m_ProgramRef.m_ProgramId)
				{
					bytes[offset++] = static_cast<std::byte>(
						static_cast<unsigned char>(character));
				}
				WriteU32LE(bytes, offset,
					static_cast<uint32_t>(entry.m_ProgramRef.m_VariantId.size()));
				for (const char character : entry.m_ProgramRef.m_VariantId)
				{
					bytes[offset++] = static_cast<std::byte>(
						static_cast<unsigned char>(character));
				}
				WriteU32LE(bytes, offset,
					static_cast<uint32_t>(entry.m_ProgramRef.m_Stage));
				bytes[offset++] = static_cast<std::byte>(entry.m_TargetProfile);
				std::ranges::copy(
					entry.m_ArtifactRef.m_ArtifactId.m_DurableDigest.m_Value,
					bytes.begin() + offset);
				offset += Sha256Digest::Size;
			}
			return offset == bytes.size()
				? bytes
				: SerializedShaderProgramRegistryArtifact{};
		}
		catch (...)
		{
			return {};
		}
	}

	std::optional<ShaderProgramRegistryArtifact> DeserializeShaderProgramRegistryArtifact(
		std::span<const std::byte> bytes) noexcept
	{
		if (bytes.size() < SerializedShaderProgramRegistryArtifactHeaderSize ||
			bytes.size() > MaxSerializedShaderProgramRegistryArtifactSize ||
			!std::ranges::equal(
				ProgramRegistryMagic, bytes.first(ProgramRegistryMagic.size())))
		{
			return std::nullopt;
		}

		try
		{
			size_t offset = ProgramRegistryMagic.size();
			uint32_t fileVersion = 0;
			uint32_t schemaVersion = 0;
			if (!TryReadU32LE(bytes, offset, fileVersion) ||
				fileVersion != ShaderProgramRegistryArtifactFileFormatVersion ||
				!TryReadU32LE(bytes, offset, schemaVersion))
			{
				return std::nullopt;
			}

			ShaderProgramRegistryArtifact artifact{};
			artifact.m_SchemaVersion = schemaVersion;
			if (!CanRead(bytes, offset, Sha256Digest::Size))
			{
				return std::nullopt;
			}
			std::ranges::copy_n(
				bytes.begin() + offset,
				Sha256Digest::Size,
				artifact.m_RegistryId.m_DurableDigest.m_Value.begin());
			offset += Sha256Digest::Size;

			uint32_t entryCount = 0;
			if (!TryReadU32LE(bytes, offset, entryCount) || entryCount == 0 ||
				entryCount > MaxShaderProgramRegistryEntryCount)
			{
				return std::nullopt;
			}
			artifact.m_Entries.reserve(entryCount);
			for (uint32_t entryIndex = 0; entryIndex < entryCount; ++entryIndex)
			{
				ShaderProgramRegistryEntry entry{};
				uint32_t programIdSize = 0;
				if (!TryReadU32LE(bytes, offset, programIdSize) || programIdSize == 0 ||
					programIdSize > MaxShaderProgramIdentityComponentSize ||
					!CanRead(bytes, offset, programIdSize))
				{
					return std::nullopt;
				}
				entry.m_ProgramRef.m_ProgramId.assign(
					reinterpret_cast<const char*>(bytes.data() + offset), programIdSize);
				offset += programIdSize;

				uint32_t variantIdSize = 0;
				if (!TryReadU32LE(bytes, offset, variantIdSize) || variantIdSize == 0 ||
					variantIdSize > MaxShaderProgramIdentityComponentSize ||
					!CanRead(bytes, offset, variantIdSize))
				{
					return std::nullopt;
				}
				entry.m_ProgramRef.m_VariantId.assign(
					reinterpret_cast<const char*>(bytes.data() + offset), variantIdSize);
				offset += variantIdSize;

				uint32_t stage = 0;
				if (!TryReadU32LE(bytes, offset, stage) || !CanRead(bytes, offset, 1))
				{
					return std::nullopt;
				}
				entry.m_ProgramRef.m_Stage = static_cast<ShaderStage>(stage);
				entry.m_TargetProfile = static_cast<ShaderTargetProfile>(
					std::to_integer<uint8_t>(bytes[offset++]));
				if (!CanRead(bytes, offset, Sha256Digest::Size))
				{
					return std::nullopt;
				}
				std::ranges::copy_n(
					bytes.begin() + offset,
					Sha256Digest::Size,
					entry.m_ArtifactRef.m_ArtifactId.m_DurableDigest.m_Value.begin());
				offset += Sha256Digest::Size;
				artifact.m_Entries.push_back(std::move(entry));
			}

			if (offset != bytes.size() ||
				ValidateShaderProgramRegistryArtifact(artifact) !=
					ShaderProgramRegistryArtifactValidationStatus::Valid)
			{
				return std::nullopt;
			}
			return artifact;
		}
		catch (...)
		{
			return std::nullopt;
		}
	}

	SerializedActiveShaderProgramRegistry SerializeActiveShaderProgramRegistry(
		const ShaderProgramRegistryArtifactRef& registryRef) noexcept
	{
		SerializedActiveShaderProgramRegistry bytes{};
		if (!registryRef.IsValid())
		{
			return bytes;
		}
		size_t offset = 0;
		std::ranges::copy(ActiveProgramRegistryMagic, bytes.begin());
		offset += ActiveProgramRegistryMagic.size();
		WriteU32LE(bytes, offset, ActiveShaderProgramRegistryFileFormatVersion);
		WriteU32LE(bytes, offset, ActiveShaderProgramRegistrySchemaVersion);
		std::ranges::copy(
			registryRef.m_RegistryId.m_DurableDigest.m_Value, bytes.begin() + offset);
		return bytes;
	}

	std::optional<ShaderProgramRegistryArtifactRef> DeserializeActiveShaderProgramRegistry(
		std::span<const std::byte> bytes) noexcept
	{
		if (bytes.size() != SerializedActiveShaderProgramRegistrySize ||
			!std::ranges::equal(
				ActiveProgramRegistryMagic, bytes.first(ActiveProgramRegistryMagic.size())))
		{
			return std::nullopt;
		}
		size_t offset = ActiveProgramRegistryMagic.size();
		if (ReadU32LE(bytes, offset) != ActiveShaderProgramRegistryFileFormatVersion ||
			ReadU32LE(bytes, offset) != ActiveShaderProgramRegistrySchemaVersion)
		{
			return std::nullopt;
		}
		ShaderProgramRegistryArtifactRef registryRef{};
		std::ranges::copy_n(
			bytes.begin() + offset,
			Sha256Digest::Size,
			registryRef.m_RegistryId.m_DurableDigest.m_Value.begin());
		return registryRef.IsValid()
			? std::optional<ShaderProgramRegistryArtifactRef>(registryRef)
			: std::nullopt;
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
				MaxSerializedShaderRuntimeArtifactManifestSize,
				serializedManifest);
			if (manifestRead == FileReadStatus::Missing)
			{
				return { .m_Status = ShaderArtifactReadStatus::NotFound };
			}
			if (manifestRead == FileReadStatus::Failure)
			{
				return { .m_Status = ShaderArtifactReadStatus::IOFailure };
			}
			if (manifestRead == FileReadStatus::Malformed)
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

	ShaderLooseProgramRegistryArtifactLocator::ShaderLooseProgramRegistryArtifactLocator(
		std::filesystem::path root) : m_Root(std::move(root))
	{
	}

	const std::filesystem::path& ShaderLooseProgramRegistryArtifactLocator::GetRoot() const noexcept
	{
		return m_Root;
	}

	ShaderLooseProgramRegistryArtifactPath ShaderLooseProgramRegistryArtifactLocator::GetPath(
		const ShaderProgramRegistryArtifactRef& registryRef) const
	{
		if (!registryRef.IsValid())
		{
			return {};
		}
		const std::string registryId =
			Sha256DigestToHex(registryRef.m_RegistryId.m_DurableDigest);
		return {
			.m_Path = m_Root / "program-registry" / registryId.substr(0, 2) /
				(registryId + ".ggsh.registry"),
		};
	}

	ShaderLooseProgramRegistryArtifactReader::ShaderLooseProgramRegistryArtifactReader(
		ShaderLooseProgramRegistryArtifactLocator locator) : m_Locator(std::move(locator))
	{
	}

	const ShaderLooseProgramRegistryArtifactLocator&
		ShaderLooseProgramRegistryArtifactReader::GetLocator() const noexcept
	{
		return m_Locator;
	}

	ShaderProgramRegistryArtifactReadResult
		ShaderLooseProgramRegistryArtifactReader::ReadArtifact(
			const ShaderProgramRegistryArtifactRef& registryRef) noexcept
	{
		if (!registryRef.IsValid() || m_Locator.GetRoot().empty())
		{
			return {
				.m_Status = ShaderProgramRegistryArtifactReadStatus::MalformedArtifact,
			};
		}

		try
		{
			const ShaderLooseProgramRegistryArtifactPath path = m_Locator.GetPath(registryRef);
			ShaderBinary serializedArtifact;
			const FileReadStatus readStatus = ReadFile(
				path.m_Path,
				MaxSerializedShaderProgramRegistryArtifactSize,
				serializedArtifact);
			if (readStatus == FileReadStatus::Missing)
			{
				return { .m_Status = ShaderProgramRegistryArtifactReadStatus::NotFound };
			}
			if (readStatus == FileReadStatus::Failure)
			{
				return { .m_Status = ShaderProgramRegistryArtifactReadStatus::IOFailure };
			}
			if (readStatus == FileReadStatus::Malformed)
			{
				return {
					.m_Status = ShaderProgramRegistryArtifactReadStatus::MalformedArtifact,
				};
			}

			const std::optional<ShaderProgramRegistryArtifact> artifact =
				DeserializeShaderProgramRegistryArtifact(std::span(
					static_cast<const std::byte*>(serializedArtifact.Data()),
					serializedArtifact.SizeInBytes()));
			if (!artifact.has_value() || artifact->m_RegistryId != registryRef.m_RegistryId)
			{
				return {
					.m_Status = ShaderProgramRegistryArtifactReadStatus::MalformedArtifact,
				};
			}
			return {
				.m_Status = ShaderProgramRegistryArtifactReadStatus::Success,
				.m_Artifact = *artifact,
			};
		}
		catch (...)
		{
			return { .m_Status = ShaderProgramRegistryArtifactReadStatus::IOFailure };
		}
	}

	ShaderLooseActiveProgramRegistryLocator::ShaderLooseActiveProgramRegistryLocator(
		std::filesystem::path root, ShaderTargetProfile targetProfile) :
		m_Root(std::move(root)), m_TargetProfile(targetProfile)
	{
	}

	const std::filesystem::path& ShaderLooseActiveProgramRegistryLocator::GetRoot() const noexcept
	{
		return m_Root;
	}

	ShaderTargetProfile ShaderLooseActiveProgramRegistryLocator::GetTargetProfile() const noexcept
	{
		return m_TargetProfile;
	}

	std::filesystem::path ShaderLooseActiveProgramRegistryLocator::GetPath() const
	{
		if (m_Root.empty() || !IsKnownShaderTargetProfile(m_TargetProfile))
		{
			return {};
		}
		const std::filesystem::path targetDirectory =
			m_TargetProfile == ShaderTargetProfile::GGLabVulkan13
			? "gglab-vulkan13"
			: "gglab-dx12";
		return m_Root / "active" / targetDirectory / "program-registry.ggsh.active";
	}

	ShaderLooseActiveProgramRegistryReader::ShaderLooseActiveProgramRegistryReader(
		ShaderLooseActiveProgramRegistryLocator locator) : m_Locator(std::move(locator))
	{
	}

	const ShaderLooseActiveProgramRegistryLocator&
		ShaderLooseActiveProgramRegistryReader::GetLocator() const noexcept
	{
		return m_Locator;
	}

	ActiveShaderProgramRegistryReadResult ShaderLooseActiveProgramRegistryReader::Read() noexcept
	{
		if (m_Locator.GetRoot().empty() ||
			!IsKnownShaderTargetProfile(m_Locator.GetTargetProfile()))
		{
			return { .m_Status = ActiveShaderProgramRegistryReadStatus::MalformedRecord };
		}
		try
		{
			ShaderBinary serializedRecord;
			const FileReadStatus readStatus = ReadFile(
				m_Locator.GetPath(), SerializedActiveShaderProgramRegistrySize, serializedRecord);
			if (readStatus == FileReadStatus::Missing)
			{
				return { .m_Status = ActiveShaderProgramRegistryReadStatus::NotFound };
			}
			if (readStatus == FileReadStatus::Failure)
			{
				return { .m_Status = ActiveShaderProgramRegistryReadStatus::IOFailure };
			}
			if (readStatus == FileReadStatus::Malformed)
			{
				return { .m_Status = ActiveShaderProgramRegistryReadStatus::MalformedRecord };
			}
			const std::optional<ShaderProgramRegistryArtifactRef> registryRef =
				DeserializeActiveShaderProgramRegistry(std::span(
					static_cast<const std::byte*>(serializedRecord.Data()),
					serializedRecord.SizeInBytes()));
			return registryRef
				? ActiveShaderProgramRegistryReadResult{
					.m_Status = ActiveShaderProgramRegistryReadStatus::Success,
					.m_RegistryRef = *registryRef,
				}
				: ActiveShaderProgramRegistryReadResult{
					.m_Status = ActiveShaderProgramRegistryReadStatus::MalformedRecord,
				};
		}
		catch (...)
		{
			return { .m_Status = ActiveShaderProgramRegistryReadStatus::IOFailure };
		}
	}
}
