#include "Core/Precompiled.h"
#include "Graphics/Asset/DerivedData/IBLBundleArtifactCodec.h"
#include "Graphics/Asset/DerivedData/TextureArtifactCodec.h"
#include "Graphics/Asset/TextureArtifact.h"

namespace gglab
{
	namespace
	{
		class Writer final
		{
		public:
			void U32(uint32_t value)
			{
				for (uint32_t index = 0; index < 4; ++index)
				{
					m_Data.push_back(static_cast<std::byte>((value >> (index * 8)) & 0xffu));
				}
			}
			void U64(uint64_t value)
			{
				for (uint32_t index = 0; index < 8; ++index)
				{
					m_Data.push_back(static_cast<std::byte>((value >> (index * 8)) & 0xffu));
				}
			}
			void Bytes(std::span<const std::byte> bytes)
			{
				m_Data.insert(m_Data.end(), bytes.begin(), bytes.end());
			}

			std::vector<std::byte> m_Data;
		};

		class Reader final
		{
		public:
			explicit Reader(std::span<const std::byte> data) noexcept : m_Data(data) {}

			bool U32(uint32_t& value) noexcept
			{
				uint64_t decoded = 0;
				if (!Unsigned(decoded, 4)) return false;
				value = static_cast<uint32_t>(decoded);
				return true;
			}
			bool U64(uint64_t& value) noexcept { return Unsigned(value, 8); }
			bool Bytes(std::span<std::byte> output) noexcept
			{
				if (output.size() > Remaining()) return false;
				std::memcpy(output.data(), m_Data.data() + m_Offset, output.size());
				m_Offset += output.size();
				return true;
			}
			bool View(uint64_t size, std::span<const std::byte>& output) noexcept
			{
				if (size > Remaining() || size > std::numeric_limits<size_t>::max()) return false;
				output = m_Data.subspan(m_Offset, static_cast<size_t>(size));
				m_Offset += static_cast<size_t>(size);
				return true;
			}
			[[nodiscard]] size_t Remaining() const noexcept
			{
				return m_Data.size() - m_Offset;
			}

		private:
			bool Unsigned(uint64_t& value, size_t width) noexcept
			{
				if (width > Remaining()) return false;
				value = 0;
				for (size_t index = 0; index < width; ++index)
				{
					value |= static_cast<uint64_t>(
						std::to_integer<uint8_t>(m_Data[m_Offset + index])) << (index * 8);
				}
				m_Offset += width;
				return true;
			}

			std::span<const std::byte> m_Data;
			size_t m_Offset = 0;
		};
	}

	std::vector<std::byte> IBLBundleArtifactCodec::Serialize(
		const IBLBundleArtifact& artifact) noexcept
	{
		if (!artifact.IsValid())
		{
			return {};
		}
		const std::array textures{
			&artifact.m_Environment,
			&artifact.m_Irradiance,
			&artifact.m_PrefilteredSpecular,
			&artifact.m_BrdfLut,
		};
		Writer writer;
		writer.U32(IBLBundleArtifactSchemaVersion);
		for (const TextureAssetData* texture : textures)
		{
			const ArtifactContentDigest textureDigest =
				ComputeTextureArtifactContentDigest(*texture);
			std::vector<std::byte> texturePayload =
				TextureArtifactCodec::SerializeTextureData(*texture);
			if (!textureDigest.IsValid() || texturePayload.empty())
			{
				return {};
			}
			writer.Bytes(textureDigest.m_Value);
			writer.U64(static_cast<uint64_t>(texturePayload.size()));
			writer.Bytes(texturePayload);
		}
		return std::move(writer.m_Data);
	}

	IBLBundleArtifactDecodeResult IBLBundleArtifactCodec::Deserialize(
		std::span<const std::byte> payload,
		const ArtifactContentDigest& expectedContentDigest) noexcept
	{
		IBLBundleArtifactDecodeResult result{};
		if (payload.empty() || !expectedContentDigest.IsValid())
		{
			result.m_Error = "IBL bundle DDC payload or content digest is empty.";
			return result;
		}

		Reader reader(payload);
		uint32_t schemaVersion = 0;
		if (!reader.U32(schemaVersion) || schemaVersion != IBLBundleArtifactSchemaVersion)
		{
			result.m_Error = "IBL bundle DDC schema is invalid.";
			return result;
		}
		std::array textures{
			&result.m_Artifact.m_Environment,
			&result.m_Artifact.m_Irradiance,
			&result.m_Artifact.m_PrefilteredSpecular,
			&result.m_Artifact.m_BrdfLut,
		};
		for (TextureAssetData* texture : textures)
		{
			ArtifactContentDigest textureDigest{};
			uint64_t texturePayloadSize = 0;
			std::span<const std::byte> texturePayload;
			if (!reader.Bytes(textureDigest.m_Value) ||
				!reader.U64(texturePayloadSize) ||
				!reader.View(texturePayloadSize, texturePayload))
			{
				result = {};
				result.m_Error = "IBL bundle DDC texture payload is truncated.";
				return result;
			}
			TextureArtifactDecodeResult decoded = TextureArtifactCodec::Deserialize(
				texturePayload,
				textureDigest);
			if (!decoded.Succeeded())
			{
				result = {};
				result.m_Error = std::format(
					"IBL bundle texture decode failed: {}",
					decoded.m_Error);
				return result;
			}
			*texture = std::move(decoded.m_Artifact.m_Data);
		}
		if (reader.Remaining() != 0)
		{
			result = {};
			result.m_Error = "IBL bundle DDC payload contains trailing bytes.";
			return result;
		}
		result.m_Artifact.m_ContentDigest =
			ComputeIBLBundleArtifactContentDigest(result.m_Artifact);
		if (result.m_Artifact.m_ContentDigest != expectedContentDigest ||
			!result.m_Artifact.IsValid())
		{
			result = {};
			result.m_Error = "IBL bundle artifact content digest mismatch.";
		}
		return result;
	}
}
