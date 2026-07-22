#include "Core/Precompiled.h"
#include "Application/SelfTest/AssetDataSelfTests.h"
#include "Core/Hash/Sha256.h"
#include "Graphics/Asset/DerivedData/DerivedDataKey.h"
#include "Graphics/Asset/DerivedData/TextureArtifactCodec.h"

namespace gglab
{
	namespace
	{
		[[nodiscard]] bool MatchesHex(
			std::span<const std::byte> bytes,
			std::string_view expected) noexcept
		{
			constexpr std::string_view HexDigits = "0123456789abcdef";
			if (expected.size() != bytes.size() * 2)
			{
				return false;
			}

			for (size_t index = 0; index < bytes.size(); ++index)
			{
				const uint8_t value = std::to_integer<uint8_t>(bytes[index]);
				if (expected[index * 2] != HexDigits[value >> 4] ||
					expected[index * 2 + 1] != HexDigits[value & 0x0f])
				{
					return false;
				}
			}
			return true;
		}

		[[nodiscard]] TextureAssetData MakeTextureFixture()
		{
			TextureAssetData texture{};
			texture.m_ResourceFormat = RHIFormat::R8G8B8A8Typeless;
			texture.m_ViewFormat = RHIFormat::R8G8B8A8UnormSrgb;
			texture.m_SrvDimension = RHITextureViewDimension::Texture2D;
			texture.m_Extent = { 2, 1, 1 };
			texture.m_ArraySize = 1;
			texture.m_MipLevels = 1;
			texture.m_ColorSpace = TextureColorSpace::SRGB;
			texture.m_Pixels = {
				std::byte{ 0x10 }, std::byte{ 0x11 }, std::byte{ 0x12 }, std::byte{ 0x13 },
				std::byte{ 0x14 }, std::byte{ 0x15 }, std::byte{ 0x16 }, std::byte{ 0x17 },
			};
			texture.m_Subresources.push_back({
				.m_DataOffset = 0,
				.m_DataSize = 8,
				.m_RowPitch = 8,
				.m_SlicePitch = 8,
				.m_Width = 2,
				.m_Height = 1,
				.m_Depth = 1,
				.m_MipLevel = 0,
				.m_ArraySlice = 0,
			});
			return texture;
		}

		[[nodiscard]] bool TextureDataMatchesFixture(const TextureAssetData& texture) noexcept
		{
			if (texture.m_ResourceFormat != RHIFormat::R8G8B8A8Typeless ||
				texture.m_ViewFormat != RHIFormat::R8G8B8A8UnormSrgb ||
				texture.m_SrvDimension != RHITextureViewDimension::Texture2D ||
				texture.m_Extent.m_Width != 2 || texture.m_Extent.m_Height != 1 ||
				texture.m_Extent.m_Depth != 1 || texture.m_ArraySize != 1 ||
				texture.m_MipLevels != 1 || texture.m_ColorSpace != TextureColorSpace::SRGB ||
				texture.m_Subresources.size() != 1 || texture.m_Pixels.size() != 8)
			{
				return false;
			}

			const TextureAssetSubresource& subresource = texture.m_Subresources.front();
			return subresource.m_DataOffset == 0 && subresource.m_DataSize == 8 &&
				subresource.m_RowPitch == 8 && subresource.m_SlicePitch == 8 &&
				subresource.m_Width == 2 && subresource.m_Height == 1 &&
				subresource.m_Depth == 1 && subresource.m_MipLevel == 0 &&
				subresource.m_ArraySlice == 0 &&
				std::ranges::equal(texture.m_Pixels, MakeTextureFixture().m_Pixels);
		}

		void RunSha256Tests(SelfTestContext& context) noexcept
		{
			const Sha256Hash emptyHash = ComputeSha256({});
			context.Check(
				MatchesHex(emptyHash.m_Value,
					"e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"),
				"SHA-256 empty input matches the standard vector");

			constexpr std::string_view Input = "abc";
			const Sha256Hash abcHash = ComputeSha256(std::as_bytes(std::span{ Input }));
			context.Check(
				MatchesHex(abcHash.m_Value,
					"ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"),
				"SHA-256 text input matches the standard vector");
		}

		void RunDerivedDataKeyTests(SelfTestContext& context) noexcept
		{
			constexpr std::string_view Source = "asset source";
			SourceDigest sourceDigest{};
			sourceDigest.m_Value = ComputeSha256(std::as_bytes(std::span{ Source })).m_Value;

			const TextureImportSettings settings{
				.m_Semantic = TextureSemantic::BaseColor,
				.m_MipPolicy = TextureMipPolicy::Preserve,
			};
			const DerivedDataKey key = BuildTextureDerivedDataKey(
				sourceDigest,
				"Textures/Fixture.PNG",
				settings);
			context.Check(
				MatchesHex(key.m_Value,
					"d23bc6a906672bcfb535fa4b25cff593186818da484f8d40e03f9f6b76d9c564"),
				"Texture derived-data key matches the stable golden vector");
		}

		void RunTextureCodecTests(SelfTestContext& context) noexcept
		{
			TextureArtifact artifact{};
			artifact.m_Data = MakeTextureFixture();
			artifact.m_ContentDigest = ComputeTextureArtifactContentDigest(artifact.m_Data);
			context.Check(
				MatchesHex(artifact.m_ContentDigest.m_Value,
					"9f01361721504e531dce2e8437dd2698515b96e47718b64cd372390e242720f1"),
				"Texture artifact content digest matches the stable golden vector");

			std::vector<std::byte> payload = TextureArtifactCodec::Serialize(artifact);
			const Sha256Hash payloadHash = ComputeSha256(payload);
			context.Check(
				payload.size() == 116 && MatchesHex(payloadHash.m_Value,
					"17755aeff6b484ac40f8a8e0f4ff1b7ee8c2a91f4e77ee58410b2623b1908e72"),
				"Texture artifact codec matches the stable payload layout");

			const TextureArtifactDecodeResult decoded = TextureArtifactCodec::Deserialize(
				payload,
				artifact.m_ContentDigest);
			context.Check(
				decoded.Succeeded() && decoded.m_Artifact.m_ContentDigest == artifact.m_ContentDigest &&
					TextureDataMatchesFixture(decoded.m_Artifact.m_Data),
				"Texture artifact codec round-trips the fixture");

			payload.back() ^= std::byte{ 0xff };
			const TextureArtifactDecodeResult corrupted = TextureArtifactCodec::Deserialize(
				payload,
				artifact.m_ContentDigest);
			context.Check(
				!corrupted.Succeeded() && !corrupted.m_Error.empty(),
				"Texture artifact codec rejects corrupted pixel data");

			payload.pop_back();
			const TextureArtifactDecodeResult truncated = TextureArtifactCodec::Deserialize(
				payload,
				artifact.m_ContentDigest);
			context.Check(
				!truncated.Succeeded() && !truncated.m_Error.empty(),
				"Texture artifact codec rejects truncated payloads");
		}
	}

	void RunAssetDataSelfTests(SelfTestContext& context) noexcept
	{
		RunSha256Tests(context);
		RunDerivedDataKeyTests(context);
		RunTextureCodecTests(context);
	}
}
