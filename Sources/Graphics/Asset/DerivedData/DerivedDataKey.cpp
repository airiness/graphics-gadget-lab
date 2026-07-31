#include "Core/Precompiled.h"
#include "Graphics/Asset/DerivedData/DerivedDataKey.h"
#include "Graphics/Asset/TextureAsset.h"

namespace gglab
{
	size_t DerivedDataKeyHash::operator()(const DerivedDataKey& key) const noexcept
	{
		uint64_t hash = 14695981039346656037ull;
		for (const std::byte value : key.m_Value)
		{
			hash ^= static_cast<uint8_t>(value);
			hash *= 1099511628211ull;
		}
		return static_cast<size_t>(hash);
	}

	namespace
	{
		template <class T> [[nodiscard]] std::string DigestText(const T& digest, size_t byteCount)
		{
			if (!digest.IsValid())
				return {};
			constexpr char HexDigits[] = "0123456789abcdef";
			const size_t count = std::min(byteCount, digest.m_Value.size());
			std::string result(count * 2, '0');
			for (size_t index = 0; index < count; ++index)
			{
				const uint8_t value = std::to_integer<uint8_t>(digest.m_Value[index]);
				result[index * 2] = HexDigits[value >> 4];
				result[index * 2 + 1] = HexDigits[value & 0x0f];
			}
			return result;
		}
	}

	DerivedDataKey DerivedDataKeyBuilder::Finish() noexcept
	{
		DerivedDataKey key{};
		key.m_Value = m_Builder.Finish().m_Value;
		return key;
	}

	DerivedDataKey BuildTextureDerivedDataKey(const SourceDigest& sourceDigest,
		const std::filesystem::path& sourceIdentity,
		const TextureImportSettings& importSettings) noexcept
	{
		if (!sourceDigest.IsValid())
			return {};
		std::string sourceExtension = sourceIdentity.extension().generic_string();
		std::ranges::transform(sourceExtension, sourceExtension.begin(),
			[](unsigned char value) noexcept
			{
				return static_cast<char>(
					value >= 'A' && value <= 'Z' ? value + ('a' - 'A') : value);
			});
		DerivedDataKeyBuilder builder;
		bool succeeded = builder.AddStringUtf8("gglab.texture");
		succeeded &= builder.AddU32(TextureArtifactSchemaVersion);
		succeeded &= builder.AddU32(TextureProducerCompatibilityVersion);
		succeeded &= builder.AddU32(TextureDecoderVersion);
		succeeded &= builder.AddSourceDigest(sourceDigest);
		succeeded &= builder.AddStringUtf8(sourceExtension);
		succeeded &= builder.AddU32(static_cast<uint32_t>(importSettings.m_Semantic));
		succeeded &= builder.AddU32(static_cast<uint32_t>(importSettings.m_MipPolicy));
		return succeeded ? builder.Finish() : DerivedDataKey{};
	}

	std::string SourceDigestText(const SourceDigest& digest, size_t byteCount)
	{
		return DigestText(digest, byteCount);
	}

	std::string DerivedDataKeyText(const DerivedDataKey& key, size_t byteCount)
	{
		return DigestText(key, byteCount);
	}
}
