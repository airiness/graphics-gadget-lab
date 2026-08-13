#include "Graphics/Asset/Store/TextureStore.h"

#include <algorithm>
#include <filesystem>
#include <memory>
#include <utility>

namespace gglab
{
	const Texture* TextureStore::Find(TextureID textureId) const noexcept
	{
		const auto iterator = m_Entries.find(textureId);
		return iterator != m_Entries.end() ? iterator->second.get() : nullptr;
	}

	Texture* TextureStore::Edit(TextureID textureId) noexcept
	{
		return const_cast<Texture*>(std::as_const(*this).Find(textureId));
	}

	TextureID TextureStore::FindCached(const std::filesystem::path& canonicalPath,
		const TextureImportSettings& importSettings) const noexcept
	{
		const auto iterator = m_CacheKeys.find(CacheKey{ canonicalPath, importSettings });
		return iterator != m_CacheKeys.end() ? iterator->second : TextureID{};
	}

	bool TextureStore::BindCacheKey(const std::filesystem::path& canonicalPath,
		const TextureImportSettings& importSettings, TextureID textureId) noexcept
	{
		return textureId.IsValid() &&
			m_CacheKeys.emplace(CacheKey{ canonicalPath, importSettings }, textureId).second;
	}

	bool TextureStore::Insert(TextureID textureId, std::unique_ptr<Texture>&& texture) noexcept
	{
		if (!textureId.IsValid() || !texture)
		{
			return false;
		}
		return m_Entries.emplace(textureId, std::move(texture)).second;
	}

	bool TextureStore::Remove(TextureID textureId) noexcept
	{
		const bool removed = m_Entries.erase(textureId) > 0;
		const size_t removedKeys = std::erase_if(m_CacheKeys,
			[textureId](const auto& entry) noexcept { return entry.second == textureId; });
		return removed || removedKeys > 0;
	}
}
