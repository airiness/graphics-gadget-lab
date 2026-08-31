#include "ShaderArtifactRuntime/ShaderPreviewLooseIO.h"

#include "GGLabFoundation/Hash/Sha256.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <fstream>
#include <utility>
#include <vector>

namespace gglab
{
	namespace
	{
		constexpr std::array<std::byte, 8> PreviewPublicationMagic{
			std::byte{ 'G' },
			std::byte{ 'G' },
			std::byte{ 'S' },
			std::byte{ 'H' },
			std::byte{ 'P' },
			std::byte{ 'R' },
			std::byte{ 'E' },
			std::byte{ 'V' },
		};
		constexpr std::array<std::byte, 8> PreviewActivePublicationMagic{
			std::byte{ 'G' },
			std::byte{ 'G' },
			std::byte{ 'S' },
			std::byte{ 'H' },
			std::byte{ 'A' },
			std::byte{ 'C' },
			std::byte{ 'T' },
			std::byte{ 'V' },
		};
		constexpr std::array<std::byte, 8> PreviewObservationMagic{
			std::byte{ 'G' },
			std::byte{ 'G' },
			std::byte{ 'S' },
			std::byte{ 'H' },
			std::byte{ 'O' },
			std::byte{ 'B' },
			std::byte{ 'S' },
			std::byte{ 'V' },
		};

		template<class Container>
		void WriteU32LE(
			Container& bytes, size_t& offset, uint32_t value) noexcept
		{
			for (size_t byteIndex = 0; byteIndex < sizeof(value); ++byteIndex)
			{
				bytes[offset++] = static_cast<std::byte>(value & 0xffu);
				value >>= 8u;
			}
		}

		template<class Container>
		void WriteU64LE(
			Container& bytes, size_t& offset, uint64_t value) noexcept
		{
			for (size_t byteIndex = 0; byteIndex < sizeof(value); ++byteIndex)
			{
				bytes[offset++] = static_cast<std::byte>(value & 0xffu);
				value >>= 8u;
			}
		}

		[[nodiscard]] bool CanRead(
			std::span<const std::byte> bytes, size_t offset, size_t count) noexcept
		{
			return offset <= bytes.size() && count <= bytes.size() - offset;
		}

		[[nodiscard]] bool TryReadU32LE(
			std::span<const std::byte> bytes,
			size_t& offset,
			uint32_t& outValue) noexcept
		{
			if (!CanRead(bytes, offset, sizeof(outValue)))
			{
				return false;
			}
			outValue = 0;
			for (size_t byteIndex = 0; byteIndex < sizeof(outValue); ++byteIndex)
			{
				outValue |= std::to_integer<uint32_t>(bytes[offset++]) <<
					(byteIndex * 8u);
			}
			return true;
		}

		[[nodiscard]] bool TryReadU64LE(
			std::span<const std::byte> bytes,
			size_t& offset,
			uint64_t& outValue) noexcept
		{
			if (!CanRead(bytes, offset, sizeof(outValue)))
			{
				return false;
			}
			outValue = 0;
			for (size_t byteIndex = 0; byteIndex < sizeof(outValue); ++byteIndex)
			{
				outValue |= std::to_integer<uint64_t>(bytes[offset++]) <<
					(byteIndex * 8u);
			}
			return true;
		}

		template<class Container>
		void WriteDigest(
			Container& bytes, size_t& offset, const Sha256Digest& digest) noexcept
		{
			std::ranges::copy(digest.m_Value, bytes.begin() + offset);
			offset += Sha256Digest::Size;
		}

		[[nodiscard]] bool TryReadDigest(
			std::span<const std::byte> bytes,
			size_t& offset,
			Sha256Digest& outDigest) noexcept
		{
			if (!CanRead(bytes, offset, Sha256Digest::Size))
			{
				return false;
			}
			std::ranges::copy_n(
				bytes.begin() + offset,
				Sha256Digest::Size,
				outDigest.m_Value.begin());
			offset += Sha256Digest::Size;
			return true;
		}

		template<class Container>
		void WriteString(
			Container& bytes, size_t& offset, std::string_view value) noexcept
		{
			WriteU32LE(bytes, offset, static_cast<uint32_t>(value.size()));
			for (const unsigned char character : value)
			{
				bytes[offset++] = static_cast<std::byte>(character);
			}
		}

		[[nodiscard]] bool TryReadString(
			std::span<const std::byte> bytes,
			size_t& offset,
			std::string& outValue)
		{
			uint32_t size = 0;
			if (!TryReadU32LE(bytes, offset, size) || size == 0 ||
				size > MaxShaderPreviewIdentityComponentSize ||
				!CanRead(bytes, offset, size))
			{
				return false;
			}
			outValue.assign(
				reinterpret_cast<const char*>(bytes.data() + offset), size);
			offset += size;
			return true;
		}

		enum class LooseReadStatus : uint8_t
		{
			Success,
			NotFound,
			IOFailure,
			Malformed,
		};

		[[nodiscard]] LooseReadStatus ReadFileBounded(
			const std::filesystem::path& path,
			uintmax_t maximumSize,
			std::vector<std::byte>& outBytes) noexcept
		{
			std::error_code errorCode;
			if (!std::filesystem::exists(path, errorCode))
			{
				return errorCode ? LooseReadStatus::IOFailure : LooseReadStatus::NotFound;
			}
			const uintmax_t fileSize = std::filesystem::file_size(path, errorCode);
			if (errorCode)
			{
				return LooseReadStatus::IOFailure;
			}
			if (fileSize == 0 || fileSize > maximumSize)
			{
				return LooseReadStatus::Malformed;
			}
			try
			{
				outBytes.resize(static_cast<size_t>(fileSize));
				std::ifstream input(path, std::ios::binary);
				if (!input)
				{
					return LooseReadStatus::IOFailure;
				}
				input.read(
					reinterpret_cast<char*>(outBytes.data()),
					static_cast<std::streamsize>(outBytes.size()));
				return input.gcount() == static_cast<std::streamsize>(outBytes.size()) &&
					input.peek() == std::char_traits<char>::eof()
					? LooseReadStatus::Success
					: LooseReadStatus::IOFailure;
			}
			catch (...)
			{
				return LooseReadStatus::IOFailure;
			}
		}
	}

	SerializedShaderPreviewPublication SerializeShaderPreviewPublication(
		const ShaderPreviewPublicationArtifact& artifact) noexcept
	{
		if (ValidateShaderPreviewPublicationArtifact(artifact) !=
			ShaderPreviewPublicationValidationStatus::Valid)
		{
			return {};
		}

		try
		{
			const size_t serializedSize = SerializedShaderPreviewPublicationFixedSize +
				artifact.m_PreviewInputContractId.size() + artifact.m_ProfileId.size() +
				artifact.m_ProgramRef.m_ProgramId.size() +
				artifact.m_ProgramRef.m_VariantId.size();
			SerializedShaderPreviewPublication bytes(serializedSize);
			size_t offset = 0;
			std::ranges::copy(PreviewPublicationMagic, bytes.begin());
			offset += PreviewPublicationMagic.size();
			WriteU32LE(bytes, offset, ShaderPreviewPublicationFileFormatVersion);
			WriteU32LE(bytes, offset, artifact.m_SchemaVersion);
			WriteDigest(bytes, offset, artifact.m_PublicationId.m_DurableDigest);
			WriteDigest(bytes, offset, artifact.m_PreviewProgramDescriptorIdentity);
			WriteString(bytes, offset, artifact.m_PreviewInputContractId);
			WriteString(bytes, offset, artifact.m_ProfileId);
			WriteU32LE(bytes, offset, artifact.m_ProfileVersion);
			WriteDigest(bytes, offset, artifact.m_GeneratedSourceIdentity);
			bytes[offset++] = static_cast<std::byte>(artifact.m_TargetProfile);
			WriteString(bytes, offset, artifact.m_ProgramRef.m_ProgramId);
			WriteString(bytes, offset, artifact.m_ProgramRef.m_VariantId);
			WriteU32LE(bytes, offset, static_cast<uint32_t>(artifact.m_ProgramRef.m_Stage));
			WriteDigest(bytes, offset,
				artifact.m_ShaderArtifactRef.m_ArtifactId.m_DurableDigest);
			WriteDigest(bytes, offset,
				artifact.m_BaseRegistryRef.m_RegistryId.m_DurableDigest);
			WriteDigest(bytes, offset,
				artifact.m_PreviewRegistryRef.m_RegistryId.m_DurableDigest);
			return offset == bytes.size()
				? bytes
				: SerializedShaderPreviewPublication{};
		}
		catch (...)
		{
			return {};
		}
	}

	std::optional<ShaderPreviewPublicationArtifact> DeserializeShaderPreviewPublication(
		std::span<const std::byte> bytes) noexcept
	{
		if (bytes.size() < SerializedShaderPreviewPublicationFixedSize ||
			bytes.size() > MaxSerializedShaderPreviewPublicationSize ||
			!std::ranges::equal(
				PreviewPublicationMagic,
				bytes.first(PreviewPublicationMagic.size())))
		{
			return std::nullopt;
		}

		try
		{
			size_t offset = PreviewPublicationMagic.size();
			uint32_t fileVersion = 0;
			ShaderPreviewPublicationArtifact artifact{};
			if (!TryReadU32LE(bytes, offset, fileVersion) ||
				fileVersion != ShaderPreviewPublicationFileFormatVersion ||
				!TryReadU32LE(bytes, offset, artifact.m_SchemaVersion) ||
				!TryReadDigest(bytes, offset, artifact.m_PublicationId.m_DurableDigest) ||
				!TryReadDigest(
					bytes, offset, artifact.m_PreviewProgramDescriptorIdentity) ||
				!TryReadString(bytes, offset, artifact.m_PreviewInputContractId) ||
				!TryReadString(bytes, offset, artifact.m_ProfileId) ||
				!TryReadU32LE(bytes, offset, artifact.m_ProfileVersion) ||
				!TryReadDigest(bytes, offset, artifact.m_GeneratedSourceIdentity) ||
				!CanRead(bytes, offset, 1))
			{
				return std::nullopt;
			}
			artifact.m_TargetProfile = static_cast<ShaderTargetProfile>(
				std::to_integer<uint8_t>(bytes[offset++]));
			uint32_t stage = 0;
			if (!TryReadString(bytes, offset, artifact.m_ProgramRef.m_ProgramId) ||
				!TryReadString(bytes, offset, artifact.m_ProgramRef.m_VariantId) ||
				!TryReadU32LE(bytes, offset, stage))
			{
				return std::nullopt;
			}
			artifact.m_ProgramRef.m_Stage = static_cast<ShaderStage>(stage);
			if (!TryReadDigest(
					bytes, offset,
					artifact.m_ShaderArtifactRef.m_ArtifactId.m_DurableDigest) ||
				!TryReadDigest(
					bytes, offset,
					artifact.m_BaseRegistryRef.m_RegistryId.m_DurableDigest) ||
				!TryReadDigest(
					bytes, offset,
					artifact.m_PreviewRegistryRef.m_RegistryId.m_DurableDigest) ||
				offset != bytes.size() ||
				ValidateShaderPreviewPublicationArtifact(artifact) !=
					ShaderPreviewPublicationValidationStatus::Valid)
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

	SerializedShaderPreviewActivePublication SerializeShaderPreviewActivePublication(
		const ShaderPreviewActivePublication& activePublication) noexcept
	{
		SerializedShaderPreviewActivePublication bytes{};
		if (!IsValidShaderPreviewActivePublication(activePublication))
		{
			return bytes;
		}
		size_t offset = 0;
		std::ranges::copy(PreviewActivePublicationMagic, bytes.begin());
		offset += PreviewActivePublicationMagic.size();
		WriteU32LE(bytes, offset, ShaderPreviewActivePublicationFileFormatVersion);
		WriteU32LE(bytes, offset, activePublication.m_SchemaVersion);
		WriteU64LE(bytes, offset, activePublication.m_AttemptSequence);
		WriteDigest(bytes, offset,
			activePublication.m_PublicationRef.m_PublicationId.m_DurableDigest);
		return bytes;
	}

	std::optional<ShaderPreviewActivePublication>
		DeserializeShaderPreviewActivePublication(
			std::span<const std::byte> bytes) noexcept
	{
		if (bytes.size() != SerializedShaderPreviewActivePublicationSize ||
			!std::ranges::equal(
				PreviewActivePublicationMagic,
				bytes.first(PreviewActivePublicationMagic.size())))
		{
			return std::nullopt;
		}
		size_t offset = PreviewActivePublicationMagic.size();
		uint32_t fileVersion = 0;
		ShaderPreviewActivePublication activePublication{};
		if (!TryReadU32LE(bytes, offset, fileVersion) ||
			fileVersion != ShaderPreviewActivePublicationFileFormatVersion ||
			!TryReadU32LE(bytes, offset, activePublication.m_SchemaVersion) ||
			!TryReadU64LE(bytes, offset, activePublication.m_AttemptSequence) ||
			!TryReadDigest(
				bytes, offset,
				activePublication.m_PublicationRef.m_PublicationId.m_DurableDigest) ||
			offset != bytes.size() ||
			!IsValidShaderPreviewActivePublication(activePublication))
		{
			return std::nullopt;
		}
		return activePublication;
	}

	SerializedShaderPreviewObservation SerializeShaderPreviewObservation(
		const ShaderPreviewObservation& observation) noexcept
	{
		SerializedShaderPreviewObservation bytes{};
		if (!IsValidShaderPreviewObservation(observation))
		{
			return bytes;
		}
		size_t offset = 0;
		std::ranges::copy(PreviewObservationMagic, bytes.begin());
		offset += PreviewObservationMagic.size();
		WriteU32LE(bytes, offset, ShaderPreviewObservationFileFormatVersion);
		WriteU32LE(bytes, offset, observation.m_SchemaVersion);
		WriteU64LE(bytes, offset, observation.m_ObservedAttemptSequence);
		WriteDigest(bytes, offset,
			observation.m_ObservedPublicationRef.m_PublicationId.m_DurableDigest);
		WriteDigest(bytes, offset,
			observation.m_LoadedPublicationRef.m_PublicationId.m_DurableDigest);
		bytes[offset++] = static_cast<std::byte>(observation.m_Status);
		bytes[offset++] = static_cast<std::byte>(observation.m_RejectionCode);
		return bytes;
	}

	std::optional<ShaderPreviewObservation> DeserializeShaderPreviewObservation(
		std::span<const std::byte> bytes) noexcept
	{
		if (bytes.size() != SerializedShaderPreviewObservationSize ||
			!std::ranges::equal(
				PreviewObservationMagic,
				bytes.first(PreviewObservationMagic.size())))
		{
			return std::nullopt;
		}
		size_t offset = PreviewObservationMagic.size();
		uint32_t fileVersion = 0;
		ShaderPreviewObservation observation{};
		if (!TryReadU32LE(bytes, offset, fileVersion) ||
			fileVersion != ShaderPreviewObservationFileFormatVersion ||
			!TryReadU32LE(bytes, offset, observation.m_SchemaVersion) ||
			!TryReadU64LE(bytes, offset, observation.m_ObservedAttemptSequence) ||
			!TryReadDigest(
				bytes, offset,
				observation.m_ObservedPublicationRef.m_PublicationId.m_DurableDigest) ||
			!TryReadDigest(
				bytes, offset,
				observation.m_LoadedPublicationRef.m_PublicationId.m_DurableDigest) ||
			!CanRead(bytes, offset, 2))
		{
			return std::nullopt;
		}
		observation.m_Status = static_cast<ShaderPreviewObservationStatus>(
			std::to_integer<uint8_t>(bytes[offset++]));
		observation.m_RejectionCode = static_cast<ShaderPreviewRejectionCode>(
			std::to_integer<uint8_t>(bytes[offset++]));
		return offset == bytes.size() && IsValidShaderPreviewObservation(observation)
			? std::optional<ShaderPreviewObservation>(observation)
			: std::nullopt;
	}

	ShaderLoosePreviewPublicationLocator::ShaderLoosePreviewPublicationLocator(
		std::filesystem::path root) : m_Root(std::move(root).lexically_normal())
	{
	}

	const std::filesystem::path& ShaderLoosePreviewPublicationLocator::GetRoot() const noexcept
	{
		return m_Root;
	}

	ShaderLoosePreviewPublicationPath ShaderLoosePreviewPublicationLocator::GetPath(
		const ShaderPreviewPublicationRef& publicationRef) const
	{
		if (!m_Root.is_absolute() || !publicationRef.IsValid())
		{
			return {};
		}
		const std::string publicationId = Sha256DigestToHex(
			publicationRef.m_PublicationId.m_DurableDigest);
		return {
			.m_Path = m_Root / "shader-preview" / publicationId.substr(0, 2) /
				(publicationId + ".ggsh.preview"),
		};
	}

	ShaderLoosePreviewPublicationReader::ShaderLoosePreviewPublicationReader(
		ShaderLoosePreviewPublicationLocator locator) : m_Locator(std::move(locator))
	{
	}

	const ShaderLoosePreviewPublicationLocator&
		ShaderLoosePreviewPublicationReader::GetLocator() const noexcept
	{
		return m_Locator;
	}

	ShaderPreviewPublicationReadResult
		ShaderLoosePreviewPublicationReader::ReadArtifact(
			const ShaderPreviewPublicationRef& publicationRef) noexcept
	{
		ShaderPreviewPublicationReadResult result{};
		try
		{
			const ShaderLoosePreviewPublicationPath path =
				m_Locator.GetPath(publicationRef);
			if (path.m_Path.empty())
			{
				result.m_Status = ShaderPreviewPublicationReadStatus::MalformedArtifact;
				return result;
			}
			std::vector<std::byte> bytes;
			const LooseReadStatus read = ReadFileBounded(
				path.m_Path, MaxSerializedShaderPreviewPublicationSize, bytes);
			if (read == LooseReadStatus::NotFound)
			{
				result.m_Status = ShaderPreviewPublicationReadStatus::NotFound;
				return result;
			}
			if (read != LooseReadStatus::Success)
			{
				if (read == LooseReadStatus::Malformed)
				{
					result.m_Status =
						ShaderPreviewPublicationReadStatus::MalformedArtifact;
				}
				return result;
			}
			const std::optional<ShaderPreviewPublicationArtifact> artifact =
				DeserializeShaderPreviewPublication(bytes);
			if (!artifact || artifact->m_PublicationId != publicationRef.m_PublicationId)
			{
				result.m_Status = ShaderPreviewPublicationReadStatus::MalformedArtifact;
				return result;
			}
			result.m_Status = ShaderPreviewPublicationReadStatus::Success;
			result.m_Artifact = *artifact;
		}
		catch (...)
		{
			return result;
		}
		return result;
	}

	ShaderLoosePreviewSessionLocator::ShaderLoosePreviewSessionLocator(
		std::filesystem::path root, std::string sessionId) :
		m_Root(std::move(root).lexically_normal()), m_SessionId(std::move(sessionId))
	{
	}

	const std::filesystem::path& ShaderLoosePreviewSessionLocator::GetRoot() const noexcept
	{
		return m_Root;
	}

	std::string_view ShaderLoosePreviewSessionLocator::GetSessionId() const noexcept
	{
		return m_SessionId;
	}

	ShaderLoosePreviewSessionPaths ShaderLoosePreviewSessionLocator::GetPaths() const
	{
		if (!m_Root.is_absolute() || !IsValidShaderPreviewSessionId(m_SessionId))
		{
			return {};
		}
		const std::filesystem::path sessionRoot =
			m_Root / "shader-preview-sessions" / m_SessionId;
		return {
			.m_ActivePublicationPath = sessionRoot / "active.ggsh.preview-active",
			.m_ObservationPath = sessionRoot / "observed.ggsh.preview-observed",
		};
	}

	ShaderLoosePreviewSessionReader::ShaderLoosePreviewSessionReader(
		ShaderLoosePreviewSessionLocator locator) : m_Locator(std::move(locator))
	{
	}

	const ShaderLoosePreviewSessionLocator&
		ShaderLoosePreviewSessionReader::GetLocator() const noexcept
	{
		return m_Locator;
	}

	ShaderPreviewActivePublicationReadResult
		ShaderLoosePreviewSessionReader::ReadActivePublication() noexcept
	{
		ShaderPreviewActivePublicationReadResult result{};
		try
		{
			const ShaderLoosePreviewSessionPaths paths = m_Locator.GetPaths();
			if (paths.m_ActivePublicationPath.empty())
			{
				result.m_Status =
					ShaderPreviewActivePublicationReadStatus::MalformedRecord;
				return result;
			}
			std::vector<std::byte> bytes;
			const LooseReadStatus read = ReadFileBounded(
				paths.m_ActivePublicationPath,
				SerializedShaderPreviewActivePublicationSize,
				bytes);
			if (read == LooseReadStatus::NotFound)
			{
				result.m_Status = ShaderPreviewActivePublicationReadStatus::NotFound;
				return result;
			}
			if (read != LooseReadStatus::Success)
			{
				if (read == LooseReadStatus::Malformed)
				{
					result.m_Status =
						ShaderPreviewActivePublicationReadStatus::MalformedRecord;
				}
				return result;
			}
			const std::optional<ShaderPreviewActivePublication> activePublication =
				DeserializeShaderPreviewActivePublication(bytes);
			if (!activePublication)
			{
				result.m_Status =
					ShaderPreviewActivePublicationReadStatus::MalformedRecord;
				return result;
			}
			result.m_Status = ShaderPreviewActivePublicationReadStatus::Success;
			result.m_ActivePublication = *activePublication;
		}
		catch (...)
		{
			return result;
		}
		return result;
	}

	ShaderPreviewObservationReadResult
		ShaderLoosePreviewSessionReader::ReadObservation() noexcept
	{
		ShaderPreviewObservationReadResult result{};
		try
		{
			const ShaderLoosePreviewSessionPaths paths = m_Locator.GetPaths();
			if (paths.m_ObservationPath.empty())
			{
				result.m_Status = ShaderPreviewObservationReadStatus::MalformedRecord;
				return result;
			}
			std::vector<std::byte> bytes;
			const LooseReadStatus read = ReadFileBounded(
				paths.m_ObservationPath,
				SerializedShaderPreviewObservationSize,
				bytes);
			if (read == LooseReadStatus::NotFound)
			{
				result.m_Status = ShaderPreviewObservationReadStatus::NotFound;
				return result;
			}
			if (read != LooseReadStatus::Success)
			{
				if (read == LooseReadStatus::Malformed)
				{
					result.m_Status =
						ShaderPreviewObservationReadStatus::MalformedRecord;
				}
				return result;
			}
			const std::optional<ShaderPreviewObservation> observation =
				DeserializeShaderPreviewObservation(bytes);
			if (!observation)
			{
				result.m_Status = ShaderPreviewObservationReadStatus::MalformedRecord;
				return result;
			}
			result.m_Status = ShaderPreviewObservationReadStatus::Success;
			result.m_Observation = *observation;
		}
		catch (...)
		{
			return result;
		}
		return result;
	}
}
