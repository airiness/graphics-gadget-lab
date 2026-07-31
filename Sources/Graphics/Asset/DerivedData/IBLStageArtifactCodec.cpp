#include "Core/Precompiled.h"
#include "Graphics/Asset/DerivedData/IBLStageArtifactCodec.h"
#include "Graphics/Asset/DerivedData/TextureArtifactCodec.h"
#include "Graphics/Asset/TextureArtifact.h"

namespace gglab
{
	namespace
	{
		void WriteU32(std::vector<std::byte>& output, uint32_t value)
		{
			for (uint32_t index = 0; index < 4; ++index)
			{
				output.push_back(static_cast<std::byte>((value >> (index * 8)) & 0xffu));
			}
		}

		void WriteU64(std::vector<std::byte>& output, uint64_t value)
		{
			for (uint32_t index = 0; index < 8; ++index)
			{
				output.push_back(static_cast<std::byte>((value >> (index * 8)) & 0xffu));
			}
		}

		bool ReadUnsigned(std::span<const std::byte> input, size_t& offset, size_t width,
			uint64_t& value) noexcept
		{
			if (width > input.size() - std::min(offset, input.size()))
			{
				return false;
			}
			value = 0;
			for (size_t index = 0; index < width; ++index)
			{
				value |= static_cast<uint64_t>(std::to_integer<uint8_t>(input[offset + index]))
					<< (index * 8);
			}
			offset += width;
			return true;
		}
	}

	std::vector<std::byte> IBLStageArtifactCodec::Serialize(
		const IBLStageArtifact& artifact) noexcept
	{
		if (!artifact.IsValid())
		{
			return {};
		}
		const ArtifactContentDigest textureDigest =
			ComputeTextureArtifactContentDigest(artifact.m_Texture);
		std::vector<std::byte> texturePayload =
			TextureArtifactCodec::SerializeTextureData(artifact.m_Texture);
		if (!textureDigest.IsValid() || texturePayload.empty())
		{
			return {};
		}

		std::vector<std::byte> payload;
		payload.reserve(4 + 4 + textureDigest.m_Value.size() + 8 + texturePayload.size());
		WriteU32(payload, IBLStageArtifactSchemaVersion);
		WriteU32(payload, static_cast<uint32_t>(artifact.m_Stage));
		payload.insert(payload.end(), textureDigest.m_Value.begin(), textureDigest.m_Value.end());
		WriteU64(payload, static_cast<uint64_t>(texturePayload.size()));
		payload.insert(payload.end(), texturePayload.begin(), texturePayload.end());
		return payload;
	}

	uint64_t IBLStageArtifactCodec::GetMaximumSerializedBytes(
		TextureAssetValidationLimits limits) noexcept
	{
		constexpr uint64_t HeaderBytes =
			2 * sizeof(uint32_t) + ArtifactContentDigest{}.m_Value.size() + sizeof(uint64_t);
		const uint64_t textureBytes = TextureArtifactCodec::GetMaximumSerializedBytes(limits);
		return textureBytes > std::numeric_limits<uint64_t>::max() - HeaderBytes
			? std::numeric_limits<uint64_t>::max()
			: HeaderBytes + textureBytes;
	}

	IBLStageArtifactDecodeResult IBLStageArtifactCodec::Deserialize(
		std::span<const std::byte> payload, IBLArtifactStage expectedStage,
		const ArtifactContentDigest& expectedContentDigest) noexcept
	{
		IBLStageArtifactDecodeResult result{};
		if (payload.empty() || expectedStage >= IBLArtifactStage::Count ||
			!expectedContentDigest.IsValid())
		{
			result.m_Error = "IBL stage DDC payload, stage, or content digest is invalid.";
			return result;
		}

		size_t offset = 0;
		uint64_t schemaVersion = 0;
		uint64_t stageValue = 0;
		if (!ReadUnsigned(payload, offset, 4, schemaVersion) ||
			schemaVersion != IBLStageArtifactSchemaVersion ||
			!ReadUnsigned(payload, offset, 4, stageValue) ||
			stageValue != static_cast<uint32_t>(expectedStage))
		{
			result.m_Error = "IBL stage DDC schema or stage is invalid.";
			return result;
		}

		ArtifactContentDigest textureDigest{};
		if (textureDigest.m_Value.size() > payload.size() - std::min(offset, payload.size()))
		{
			result.m_Error = "IBL stage DDC texture digest is truncated.";
			return result;
		}
		std::memcpy(
			textureDigest.m_Value.data(), payload.data() + offset, textureDigest.m_Value.size());
		offset += textureDigest.m_Value.size();

		uint64_t texturePayloadSize = 0;
		if (!ReadUnsigned(payload, offset, 8, texturePayloadSize) ||
			texturePayloadSize != payload.size() - offset)
		{
			result.m_Error = "IBL stage DDC texture payload is truncated or has trailing bytes.";
			return result;
		}
		TextureArtifactDecodeResult decoded =
			TextureArtifactCodec::Deserialize(payload.subspan(offset), textureDigest);
		if (!decoded.Succeeded())
		{
			result.m_Error = std::format("IBL stage texture decode failed: {}", decoded.m_Error);
			return result;
		}

		result.m_Artifact.m_Stage = expectedStage;
		result.m_Artifact.m_Texture = std::move(decoded.m_Artifact.m_Data);
		result.m_Artifact.m_ContentDigest = ComputeIBLStageArtifactContentDigest(result.m_Artifact);
		if (result.m_Artifact.m_ContentDigest != expectedContentDigest ||
			!result.m_Artifact.IsValid())
		{
			result = {};
			result.m_Error = "IBL stage artifact content digest mismatch.";
		}
		return result;
	}
}
