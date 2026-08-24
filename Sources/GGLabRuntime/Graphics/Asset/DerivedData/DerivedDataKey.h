#pragma once
#include "GGLabFoundation/Hash/Sha256.h"
#include "Graphics/GraphicsTypes.h"

#include <array>
#include <cstddef>
#include <filesystem>
#include <string>

namespace gglab
{
	struct SourceDigest
	{
		std::array<std::byte, 32> m_Value{};
		[[nodiscard]] constexpr bool IsValid() const noexcept
		{
			for (const std::byte value : m_Value)
			{
				if (value != std::byte{})
					return true;
			}
			return false;
		}
		friend constexpr bool operator==(
			const SourceDigest&, const SourceDigest&) noexcept = default;
	};

	struct DerivedDataKey
	{
		std::array<std::byte, 32> m_Value{};
		[[nodiscard]] constexpr bool IsValid() const noexcept
		{
			for (const std::byte value : m_Value)
			{
				if (value != std::byte{})
					return true;
			}
			return false;
		}
		friend constexpr bool operator==(
			const DerivedDataKey&, const DerivedDataKey&) noexcept = default;
	};

	struct DerivedDataKeyHash
	{
		[[nodiscard]] size_t operator()(const DerivedDataKey& key) const noexcept;
	};

	class DerivedDataKeyBuilder final
	{
	public:
		bool AddU32LE(uint32_t value) noexcept { return m_Builder.AddU32LE(value); }
		bool AddU64LE(uint64_t value) noexcept { return m_Builder.AddU64LE(value); }
		bool AddStringUtf8(std::string_view value) noexcept
		{
			return m_Builder.AddStringUtf8(value);
		}
		bool AddSourceDigest(const SourceDigest& digest) noexcept
		{
			return digest.IsValid() && m_Builder.AddBytes(digest.m_Value);
		}
		bool AddDerivedDataKey(const DerivedDataKey& key) noexcept
		{
			return key.IsValid() && m_Builder.AddBytes(key.m_Value);
		}
		bool AddSha256Digest(const Sha256Digest& digest) noexcept
		{
			return digest.IsValid() && m_Builder.AddBytes(digest.m_Value);
		}
		[[nodiscard]] DerivedDataKey Finish() noexcept;

	private:
		Sha256Builder m_Builder;
	};

	inline constexpr uint32_t TextureArtifactSchemaVersion = 2;
	inline constexpr uint32_t TextureProducerCompatibilityVersion = 1;

	[[nodiscard]] DerivedDataKey BuildTextureDerivedDataKey(const SourceDigest& sourceDigest,
		const std::filesystem::path& sourceIdentity,
		const TextureImportSettings& importSettings) noexcept;
	[[nodiscard]] std::string SourceDigestText(const SourceDigest& digest, size_t byteCount = 8);
	[[nodiscard]] std::string DerivedDataKeyText(const DerivedDataKey& key, size_t byteCount = 8);
}
