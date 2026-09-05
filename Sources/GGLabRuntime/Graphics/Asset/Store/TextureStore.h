#pragma once
#include "GGLabFoundation/Base/CoreMacros.h"
#include "GGLabRuntime/Core/Hash/KeyHash.h"
#include "Graphics/Asset/TextureAsset.h"

#include <cstddef>
#include <filesystem>
#include <memory>
#include <tuple>
#include <unordered_map>

namespace gglab
{
	class TextureStore final
	{
		struct CacheKey
		{
			std::filesystem::path m_CanonicalPath;
			TextureImportSettings m_ImportSettings{};

			[[nodiscard]] auto AsTuple() const noexcept
			{
				return std::tuple{
					std::filesystem::hash_value(m_CanonicalPath),
					m_ImportSettings.m_Semantic,
					m_ImportSettings.m_MipPolicy,
				};
			}
			bool operator==(const CacheKey&) const noexcept = default;
		};
		using CacheKeyHash = KeyHash<CacheKey>;

	public:
		using EntryMap = std::unordered_map<TextureID, std::unique_ptr<Texture>>;

		TextureStore() noexcept = default;
		GGLAB_DELETE_COPYABLE_MOVABLE(TextureStore);

		[[nodiscard]] const Texture* Find(TextureID textureId) const noexcept;
		[[nodiscard]] Texture* Edit(TextureID textureId) noexcept;
		[[nodiscard]] TextureID FindCached(const std::filesystem::path& canonicalPath,
			const TextureImportSettings& importSettings) const noexcept;
		[[nodiscard]] bool BindCacheKey(const std::filesystem::path& canonicalPath,
			const TextureImportSettings& importSettings, TextureID textureId) noexcept;
		[[nodiscard]] bool Insert(TextureID textureId, std::unique_ptr<Texture>&& texture) noexcept;
		[[nodiscard]] bool Remove(TextureID textureId) noexcept;

		[[nodiscard]] const EntryMap& Entries() const noexcept { return m_Entries; }
		[[nodiscard]] size_t Size() const noexcept { return m_Entries.size(); }

	private:
		std::unordered_map<CacheKey, TextureID, CacheKeyHash> m_CacheKeys;
		EntryMap m_Entries;
	};
}
