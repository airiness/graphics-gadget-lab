#pragma once
#include "GGLabFoundation/Base/CoreMacros.h"
#include "GGLabRuntime/Graphics/Asset/AssetCacheConfig.h"
#include "GGLabRuntime/Graphics/Asset/ArtifactCacheCore.h"
#include "GGLabRuntime/Graphics/Asset/TextureArtifact.h"

#include <cstdint>
#include <mutex>

namespace gglab
{
	using TextureArtifactCacheStatistics = ArtifactCacheCoreStatistics;

	class TextureArtifactCache final
	{
	public:
		explicit TextureArtifactCache(const TextureArtifactCacheConfig& config = {}) noexcept;
		GGLAB_DELETE_COPYABLE_MOVABLE(TextureArtifactCache);
		~TextureArtifactCache() = default;

		[[nodiscard]] TextureArtifactHandle CreateAndAdmit(TextureAssetData&& data) noexcept;
		[[nodiscard]] TextureArtifactHandle Admit(TextureArtifactHandle artifact) noexcept;
		[[nodiscard]] TextureArtifactHandle Find(
			const ArtifactContentDigest& contentDigest) noexcept;
		[[nodiscard]] bool Contains(const ArtifactContentDigest& contentDigest) const noexcept;
		void Clear() noexcept;
		[[nodiscard]] TextureArtifactCacheStatistics GetStatistics() const noexcept;

	private:
		using Core =
			ArtifactCacheCore<ArtifactContentDigest, TextureArtifact, ArtifactContentDigestHash>;

		mutable std::mutex m_Mutex;
		Core m_Core;
	};
}
