#include "Core/Precompiled.h"
#include "Graphics/Asset/DerivedData/TextureArtifactCodec.h"
#include "Graphics/Asset/DerivedData/DerivedDataKey.h"

namespace gglab
{
	namespace
	{
		class Writer
		{
		public:
			void U32(uint32_t value) { for (uint32_t i = 0; i < 4; ++i) m_Data.push_back(static_cast<std::byte>((value >> (i * 8)) & 0xffu)); }
			void U64(uint64_t value) { for (uint32_t i = 0; i < 8; ++i) m_Data.push_back(static_cast<std::byte>((value >> (i * 8)) & 0xffu)); }
			void Bytes(std::span<const std::byte> value) { m_Data.insert(m_Data.end(), value.begin(), value.end()); }
			std::vector<std::byte> m_Data;
		};

		class Reader
		{
		public:
			explicit Reader(std::span<const std::byte> data) noexcept : m_Data(data) {}
			bool U32(uint32_t& value) noexcept { uint64_t decoded = 0; if (!Unsigned(decoded, 4)) return false; value = static_cast<uint32_t>(decoded); return true; }
			bool U64(uint64_t& value) noexcept { return Unsigned(value, 8); }
			bool Bytes(std::span<std::byte> value) noexcept
			{
				if (value.size() > Remaining()) return false;
				std::memcpy(value.data(), m_Data.data() + m_Offset, value.size());
				m_Offset += value.size();
				return true;
			}
			[[nodiscard]] size_t Remaining() const noexcept { return m_Data.size() - m_Offset; }
		private:
			bool Unsigned(uint64_t& value, size_t width) noexcept
			{
				if (width > Remaining()) return false;
				value = 0;
				for (size_t i = 0; i < width; ++i) value |= static_cast<uint64_t>(std::to_integer<uint8_t>(m_Data[m_Offset + i])) << (i * 8);
				m_Offset += width;
				return true;
			}
			std::span<const std::byte> m_Data;
			size_t m_Offset = 0;
		};
	}

	std::vector<std::byte> TextureArtifactCodec::Serialize(
		const TextureArtifact& artifact) noexcept
	{
		if (!artifact.IsValid()) return {};
		return SerializeTextureData(artifact.m_Data);
	}

	std::vector<std::byte> TextureArtifactCodec::SerializeTextureData(
		const TextureAssetData& texture) noexcept
	{
		if (!texture.IsValid()) return {};
		Writer writer;
		writer.U32(TextureArtifactSchemaVersion);
		writer.U32(static_cast<uint32_t>(texture.m_ResourceFormat));
		writer.U32(static_cast<uint32_t>(texture.m_ViewFormat));
		writer.U32(static_cast<uint32_t>(texture.m_SrvDimension));
		writer.U32(texture.m_Extent.m_Width);
		writer.U32(texture.m_Extent.m_Height);
		writer.U32(texture.m_Extent.m_Depth);
		writer.U32(texture.m_ArraySize);
		writer.U32(texture.m_MipLevels);
		writer.U32(static_cast<uint32_t>(texture.m_ColorSpace));
		writer.U64(texture.m_Subresources.size());
		for (const TextureAssetSubresource& subresource : texture.m_Subresources)
		{
			writer.U64(subresource.m_DataOffset);
			writer.U64(subresource.m_DataSize);
			writer.U64(subresource.m_RowPitch);
			writer.U64(subresource.m_SlicePitch);
			writer.U32(subresource.m_Width);
			writer.U32(subresource.m_Height);
			writer.U32(subresource.m_Depth);
			writer.U32(subresource.m_MipLevel);
			writer.U32(subresource.m_ArraySlice);
		}
		writer.U64(texture.m_Pixels.size());
		writer.Bytes(texture.m_Pixels);
		return std::move(writer.m_Data);
	}

	TextureArtifactDecodeResult TextureArtifactCodec::Deserialize(
		std::span<const std::byte> payload,
		const ArtifactContentDigest& expectedContentDigest) noexcept
	{
		TextureArtifactDecodeResult result{};
		if (payload.empty() || !expectedContentDigest.IsValid())
		{
			result.m_Error = "Texture DDC payload or content digest is empty.";
			return result;
		}
		Reader reader(payload);
		TextureAssetData& texture = result.m_Artifact.m_Data;
		uint32_t schema = 0;
		uint32_t resourceFormat = 0, viewFormat = 0, srvDimension = 0;
		uint32_t arraySize = 0, mipLevels = 0, colorSpace = 0;
		uint64_t subresourceCount = 0;
		if (!reader.U32(schema) || schema != TextureArtifactSchemaVersion ||
			!reader.U32(resourceFormat) || !reader.U32(viewFormat) ||
			!reader.U32(srvDimension) || !reader.U32(texture.m_Extent.m_Width) ||
			!reader.U32(texture.m_Extent.m_Height) || !reader.U32(texture.m_Extent.m_Depth) ||
			!reader.U32(arraySize) || !reader.U32(mipLevels) ||
			!reader.U32(colorSpace) || !reader.U64(subresourceCount) ||
			arraySize > std::numeric_limits<uint16_t>::max() ||
			mipLevels > std::numeric_limits<uint16_t>::max() ||
			subresourceCount > reader.Remaining() / 52)
		{
			result.m_Error = "Texture DDC payload header is invalid.";
			return result;
		}
		texture.m_ResourceFormat = static_cast<RHIFormat>(resourceFormat);
		texture.m_ViewFormat = static_cast<RHIFormat>(viewFormat);
		texture.m_SrvDimension = static_cast<RHITextureViewDimension>(srvDimension);
		texture.m_ArraySize = static_cast<uint16_t>(arraySize);
		texture.m_MipLevels = static_cast<uint16_t>(mipLevels);
		texture.m_ColorSpace = static_cast<TextureColorSpace>(colorSpace);
		texture.m_Subresources.resize(static_cast<size_t>(subresourceCount));
		for (TextureAssetSubresource& subresource : texture.m_Subresources)
		{
			if (!reader.U64(subresource.m_DataOffset) || !reader.U64(subresource.m_DataSize) ||
				!reader.U64(subresource.m_RowPitch) || !reader.U64(subresource.m_SlicePitch) ||
				!reader.U32(subresource.m_Width) || !reader.U32(subresource.m_Height) ||
				!reader.U32(subresource.m_Depth) || !reader.U32(subresource.m_MipLevel) ||
				!reader.U32(subresource.m_ArraySlice))
			{
				result = {};
				result.m_Error = "Texture DDC subresource table is truncated.";
				return result;
			}
		}
		uint64_t pixelBytes = 0;
		if (!reader.U64(pixelBytes) || pixelBytes != reader.Remaining() ||
			pixelBytes > std::numeric_limits<size_t>::max())
		{
			result = {};
			result.m_Error = "Texture DDC pixel payload size is invalid.";
			return result;
		}
		texture.m_Pixels.resize(static_cast<size_t>(pixelBytes));
		if (!reader.Bytes(texture.m_Pixels) || reader.Remaining() != 0 || !texture.IsValid())
		{
			result = {};
			result.m_Error = "Texture DDC artifact failed structural validation.";
			return result;
		}
		result.m_Artifact.m_ContentDigest = ComputeTextureArtifactContentDigest(texture);
		if (result.m_Artifact.m_ContentDigest != expectedContentDigest)
		{
			result = {};
			result.m_Error = "Texture DDC artifact content digest mismatch.";
		}
		return result;
	}
}
