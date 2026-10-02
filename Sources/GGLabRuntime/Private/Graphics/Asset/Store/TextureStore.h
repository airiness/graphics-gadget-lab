#pragma once
#include "GGLabFoundation/Base/CoreMacros.h"
#include "GGLabRuntime/Graphics/Asset/TextureAsset.h"
#include "Graphics/Asset/TextureSourceKey.h"

#include <cstddef>
#include <filesystem>
#include <memory>
#include <unordered_map>

namespace gglab
{
	class TextureStore final
	{
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
		std::unordered_map<TextureSourceKey, TextureID, TextureSourceKeyHash> m_CacheKeys;
		EntryMap m_Entries;
	};
}
